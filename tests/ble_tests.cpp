// Production BLE adapter and real BTstack HID parser, with radio/HIDS endpoints
// mocked at their public APIs. No controller or hardware is needed.
#include "ble.hpp"
#include "gamepad.hpp"
#include "storage.hpp"
#include "btstack.h"
#include "pico/stdlib.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
uint32_t clock_ms=0;
bool disk=false,valid_map=true;
bool sync_cancel=false,sync_disconnect=false,silent_power_off=false;
bool cancel_blocks_scan=false,scan_blocked=false;
HCI_STATE radio_state=HCI_STATE_OFF;
uint32_t poll_delay=0;
unsigned inits=0,polls=0,deinits=0,scans=0,connects=0,disconnects=0,cancels=0,reads=0,activity=0;
unsigned off=0,on=0;
uint8_t connect_result=0,discovery_result=0;
uint8_t last_read_id=0;
btstack_packet_handler_t hci_handler=nullptr,hid_handler=nullptr,sm_handler=nullptr;
bd_addr_t connected_peer{};
bd_addr_type_t connected_type=BD_ADDR_TYPE_UNKNOWN;
// Captured from the physical XTARK controller, including its consumer report 3
// and ten-byte gamepad report 4. Keep the real descriptor as a regression case.
constexpr uint8_t map[]={
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,0x75,0x10,0x95,0x01,
    0x15,0x01,0x26,0x9c,0x02,0x19,0x01,0x2a,0x9c,0x02,0x81,0x00,0xc0,
    0x05,0x01,0x09,0x05,0xa1,0x01,0x85,0x04,0x09,0x01,0xa1,0x00,
    0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x15,0x00,0x26,0xff,0x00,
    0x75,0x08,0x95,0x04,0x81,0x02,0xc0,0x09,0x39,0x15,0x00,0x25,0x07,
    0x35,0x00,0x46,0x3b,0x01,0x65,0x14,0x75,0x04,0x95,0x01,0x81,0x42,
    0x75,0x04,0x95,0x01,0x81,0x01,0x05,0x09,0x19,0x01,0x29,0x10,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x10,0x81,0x02,0x05,0x02,
    0x15,0x00,0x26,0xff,0x00,0x09,0xc4,0x09,0xc5,0x95,0x02,0x75,0x08,
    0x81,0x02,0x75,0x08,0x95,0x01,0x81,0x01,0xc0};
static_assert(sizeof(map)==122);
constexpr uint8_t keyboard_map[]={0x05,0x01,0x09,0x06,0xa1,0x01,
    0x75,0x08,0x95,0x09,0x09,0x30,0x81,0x02,0xc0};
std::vector<uint8_t> descriptor_override;
void event(std::vector<uint8_t> packet) {
    if(packet[0]==BTSTACK_EVENT_STATE) radio_state=static_cast<HCI_STATE>(packet[2]);
    assert(hci_handler); hci_handler(HCI_EVENT_PACKET,0,packet.data(),packet.size());
}
void hid(std::vector<uint8_t> packet) {
    assert(hid_handler); hid_handler(HCI_EVENT_PACKET,0,packet.data(),packet.size());
}
void advertise(uint16_t uuid=0x1812,uint8_t type=1,uint8_t address=0xaa) {
    if(scan_blocked) return;
    event({GAP_EVENT_ADVERTISING_REPORT,14,0,type,0xff,0xee,0xdd,0xcc,0xbb,address,0xb0,4,
           3,3,uint8_t(uuid),uint8_t(uuid>>8)});
}
void linked(uint8_t status=0) {
    std::vector<uint8_t> e(36);
    e[0]=HCI_EVENT_META_GAP; e[1]=34; e[2]=GAP_SUBEVENT_LE_CONNECTION_COMPLETE;
    e[3]=status; e[4]=0x40; event(e);
}
void ready(uint8_t status=0) {
    hid({HCI_EVENT_GATTSERVICE_META,6,GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED,7,0,status,1,1});
}
void sample(std::initializer_list<uint8_t> data) {
    std::vector<uint8_t> packet={HCI_EVENT_GATTSERVICE_META,0,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,4,
        uint8_t(data.size()+1),0,4};
    packet.insert(packet.end(),data); packet[1]=packet.size()-2;
    hid(packet);
}
void report(uint8_t hat,uint8_t ab=0,uint8_t menu=0) {
    sample({0x80,0x80,0x80,0x80,hat,ab,menu,0,0,0});
}
void disconnected() {event({HCI_EVENT_DISCONNECTION_COMPLETE,4,0,0x40,0,0x13});}
void pairing(uint8_t status,uint8_t reason) {
    std::vector<uint8_t> packet(14);
    packet[0]=SM_EVENT_PAIRING_COMPLETE; packet[1]=12; packet[2]=0x40;
    packet[11]=status; packet[12]=reason;
    assert(sm_handler); sm_handler(HCI_EVENT_PACKET,0,packet.data(),packet.size());
}
void advance(uint32_t ms) {clock_ms+=ms; ble::poll();}
void finish_restart() {
    unsigned previous_scans=scans,previous_on=on;
    assert(off && radio_state==HCI_STATE_HALTING);
    advance(1000); assert(scans==previous_scans && on==previous_on);
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_OFF}); ble::poll();
    assert(scans==previous_scans && on==previous_on);
    advance(1000); assert(on==previous_on+1 && scans==previous_scans && inits==1);
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==previous_scans+1);
}

}
absolute_time_t get_absolute_time() {return uint64_t(clock_ms)*1000;}
int cyw43_arch_init() {++inits; return 0;}
void cyw43_arch_poll() {++polls; clock_ms+=poll_delay; poll_delay=0;}
void cyw43_arch_deinit() {++deinits;}
namespace storage {Config config; bool state_ready() {return true;}}
namespace usb {bool dirty=false; bool disk_mode() {return disk;}}
namespace power {void activity() {++::activity;}}
extern "C" {
void btstack_assert_failed(const char *,uint16_t) {assert(false);}
void hci_add_event_handler(btstack_packet_callback_registration_t *r) {hci_handler=r->callback;}
void sm_add_event_handler(btstack_packet_callback_registration_t *r) {sm_handler=r->callback;}
int hci_power_control(HCI_POWER_MODE mode) {
    if(mode==HCI_POWER_ON) {++on; radio_state=HCI_STATE_INITIALIZING; scan_blocked=false;}
    else {++off; radio_state=silent_power_off?HCI_STATE_OFF:HCI_STATE_HALTING;}
    return 0;
}
HCI_STATE hci_get_state() {return radio_state;}
void l2cap_init() {}
void sm_init() {}
void gatt_client_init() {}
void sm_set_io_capabilities(io_capability_t) {}
void sm_set_secure_connections_only_mode(bool) {}
void sm_set_authentication_requirements(uint8_t) {}
void sm_just_works_confirm(hci_con_handle_t) {}
void hids_host_init(uint8_t *,uint16_t) {}
void gap_set_connection_parameters(uint16_t,uint16_t,uint16_t,uint16_t,uint16_t,uint16_t,uint16_t,uint16_t) {}
void gap_set_scan_parameters(uint8_t,uint16_t,uint16_t) {}
void gap_start_scan() {++scans;}
void gap_stop_scan() {}
uint8_t gap_connect(const bd_addr_t address,bd_addr_type_t type) {
    ++connects; std::memcpy(connected_peer,address,6); connected_type=type; return connect_result;
}
uint8_t gap_connect_cancel() {
    ++cancels;
    if(cancel_blocks_scan) scan_blocked=true;
    if(sync_cancel) linked(ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    return 0;
}
uint8_t gap_disconnect(hci_con_handle_t) {
    ++disconnects;
    if(sync_disconnect) disconnected();
    return 0;
}
uint8_t hids_host_connect(hci_con_handle_t,btstack_packet_handler_t callback,hid_protocol_mode_t,uint16_t *cid) {
    hid_handler=callback; *cid=7; return discovery_result;
}
uint8_t hids_host_send_get_report(uint16_t,uint8_t id,hid_report_type_t) {++reads; last_read_id=id; return 0;}
const uint8_t *hids_host_descriptor_storage_get_descriptor_data(uint16_t,uint8_t) {
    return !valid_map?keyboard_map:descriptor_override.empty()?map:descriptor_override.data();
}
uint16_t hids_host_descriptor_storage_get_descriptor_len(uint16_t,uint8_t) {
    return !valid_map?sizeof(keyboard_map):descriptor_override.empty()?sizeof(map):descriptor_override.size();
}
}
int main(int argc,char **argv) {
    if(argc==2) {
        ble::init(); assert(inits==1 && on==1);
        if(!std::strcmp(argv[1],"left_stick")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            sample({80,128,128,128,255,1,0,0,0,0}); assert(ble::buttons()==(gamepad::Left|gamepad::A));
            sample({90,128,128,128,255,0,0,0,0,0}); assert(ble::buttons()==gamepad::Left);
            sample({96,128,128,128,255,0,0,0,0,0}); assert(!ble::buttons());
            sample({95,128,128,128,255,0,0,0,0,0}); assert(!ble::buttons());
            sample({176,128,128,128,255,0,0,0,0,0}); assert(ble::buttons()==gamepad::Right);
            sample({165,128,128,128,255,0,0,0,0,0}); assert(ble::buttons()==gamepad::Right);
            sample({160,128,128,128,255,0,0,0,0,0}); assert(!ble::buttons());
            sample({0,0,128,128,255,0,0,0,0,0}); assert(ble::buttons()==(gamepad::Left|gamepad::Up));
            sample({0,0,128,128,4,0,0,0,0,0}); assert(ble::buttons()==gamepad::Down); // D-pad wins.
            sample({128,128,0,255,255,0,0,0,0,0}); assert(!ble::buttons()); // Right stick is unassigned.
            sample({80,128,128,128,255,0,0,0,0,0}); assert(ble::buttons()==gamepad::Left);
            disconnected(); advance(1000); advertise(); linked(); ready();
            sample({90,128,128,128,255,0,0,0,0,0}); assert(!ble::buttons()); // No stale hysteresis.
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"home_combo")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            report(255,0,0x60); advance(1499); assert(!ble::home() && !ble::buttons());
            report(255,0,0x60); // Repeated reports must not restart the hold timer.
            advance(1); assert(ble::home() && !ble::buttons());
            unsigned fired_activity=activity; advance(10000); assert(activity==fired_activity);
            report(255,1,0x20); assert(ble::home() && !ble::buttons()); // Release both to clear latch.
            report(255); assert(!ble::home()); report(255,1); assert(ble::buttons()==gamepad::A);
            report(255,0,0x60); advance(1000); report(255); advance(1000); assert(!ble::home());
            report(255,0,0x20); advance(2000); assert(!ble::home()); // One click is insufficient.
            report(0,0,0x60); advance(2000); assert(!ble::home()); // D-pad pressed.
            report(255,1,0x60); advance(2000); assert(!ble::home()); // Another button pressed.
            sample({0,128,128,128,255,0,0x60,0,0,0}); advance(2000); assert(!ble::home());
            sample({128,128,255,128,255,0,0x60,0,0,0}); advance(2000); assert(!ble::home());
            sample({128,128,128,128,255,0,0x60,0,1,0}); advance(2000); assert(!ble::home());
            report(255,0,0x60); advance(1000);
            hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,4});
            advance(1000); assert(!ble::home()); // Malformed input cancels the gesture.
            report(255,0,0x60); disconnected(); advance(2000); assert(!ble::home());
            advertise(); linked(); ready();
            clock_ms=0xffffff00u; report(255,0,0x60); advance(1500); assert(ble::home());
            report(255); assert(!ble::home()); report(255,0,0x10); assert(ble::home()); // Android Home.
            assert(!disconnects && !off); ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"remote_close")) {
            assert(gamepad::disconnect_report(gamepad::DisconnectReport,10));
            assert(!gamepad::disconnect_report(gamepad::DisconnectReport,9));
            uint8_t near[10]; std::memcpy(near,gamepad::DisconnectReport,10); near[9]=0;
            assert(!gamepad::disconnect_report(near,10));
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            report(2,1,0x10); assert(ble::buttons() && ble::home());
            sample({0x54,0x47,0x50,0x01,0x42,0x59,0x45,0,0,0xa5});
            assert(!ble::buttons() && !ble::home() && !disconnects); // Deferred outside callback.
            report(0,1); assert(!ble::buttons()); // Drop already queued input after the close request.
            ble::poll(); assert(disconnects==1 && !off && storage::config.ble_enabled && !usb::dirty);
            disconnected(); advance(1000); assert(scans==2 && on==1);
            advertise(); linked(); ready(); report(2,1); assert(ble::buttons()==(gamepad::Right|gamepad::A));
            sample({0x54,0x47,0x50,0x01,0x42,0x59,0x45,0,0}); // Truncated control packet cannot close.
            ble::poll(); assert(disconnects==1);
            sync_disconnect=true;
            sample({0x54,0x47,0x50,0x01,0x42,0x59,0x45,0,0,0xa5});
            ble::poll(); assert(disconnects==2 && !ble::buttons());
            advance(1000); assert(scans==3 && !off); // Synchronous link completion is safe too.
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"physical_layout")) {
            assert(btstack_hid_get_report_size_for_id(4,HID_REPORT_TYPE_INPUT,map,sizeof(map))==10);
            assert(btstack_hid_get_report_size_for_id(3,HID_REPORT_TYPE_INPUT,map,sizeof(map))==2);
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(0x1812,0); linked(); ready();
            assert(reads==1 && last_read_id==4 && !disconnects);
            report(0xa2,1,0x10); // High hat nibble is padding; low nibble means right.
            assert(ble::buttons()==(gamepad::Right|gamepad::A) && ble::home());
            // Consumer-control report 3 must neither become keys nor release them.
            hid({HCI_EVENT_GATTSERVICE_META,10,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,3,3,0,3,0,0});
            assert(ble::buttons()==(gamepad::Right|gamepad::A) && ble::home());
            // The old Android nine-byte report no longer matches this protocol.
            hid({HCI_EVENT_GATTSERVICE_META,17,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,4,10,0,
                 4,128,128,128,128,2,1,0,0,0});
            assert(!ble::buttons() && !ble::home());
            for(unsigned null:{8,15,255}) {report(null); assert(!ble::buttons());}
            report(0,2,0x0c); assert(ble::buttons()==(gamepad::Up|gamepad::B|gamepad::Select|gamepad::Start));
            disconnected(); advance(1000);
            // A descriptor with shifted buttons has the same rounded byte size
            // and hat offset, but cannot safely use the fixed XTARK byte offsets.
            descriptor_override.assign(std::begin(map),std::end(map));
            const uint8_t padding[]={0x75,0x04,0x95,0x01,0x81,0x01};
            auto pos=std::search(descriptor_override.begin(),descriptor_override.end(),std::begin(padding),std::end(padding));
            assert(pos!=descriptor_override.end()); pos[1]=3;
            advertise(); linked(); ready(); assert(disconnects==1 && reads==1);
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"pairing_failure")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(0x1812,0); linked();
            // GATT receives the ATT failure before the app's pairing callback.
            ready(ERROR_CODE_UNSPECIFIED_ERROR); assert(disconnects==1);
            pairing(ERROR_CODE_AUTHENTICATION_FAILURE,SM_REASON_PAIRING_NOT_SUPPORTED);
            assert(disconnects==1); // Same failure must not reset the disconnect timeout.
            disconnected(); advance(1000);
            advertise(0x1812,0,0xab); linked();
            // Reversing callback order must also terminate only once.
            pairing(ERROR_CODE_AUTHENTICATION_FAILURE,SM_REASON_AUTHENTHICATION_REQUIREMENTS);
            ready(ERROR_CODE_UNSPECIFIED_ERROR); assert(disconnects==2);
            disconnected(); advance(1000);
            advertise(0x1812,0,0xac); linked(); pairing(0,0); ready(); report(2,1);
            assert(disconnects==2 && ble::buttons()==(gamepad::Right|gamepad::A));
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"idle_link")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            assert(reads==1);
            report(2,1,0x10); unsigned held_activity=activity;
            // Simulated notification silence must not expire a held HID state.
            for(unsigned i=0;i<600;++i) advance(1000);
            assert(ble::buttons()==(gamepad::Right|gamepad::A) && ble::home() && activity==held_activity);
            assert(!disconnects && !off && scans==1 && reads==1);
            report(255); unsigned idle_activity=activity;
            // No app-close or service-loss probe runs after HID is READY.
            for(unsigned i=0;i<600;++i) advance(1000);
            assert(!ble::buttons() && !ble::home() && activity==idle_activity);
            assert(!disconnects && !off && scans==1 && reads==1);
            report(0,2,0x10);
            // A disconnection for another handle cannot release this controller.
            event({HCI_EVENT_DISCONNECTION_COMPLETE,4,0,0x41,0,0x13});
            assert(ble::buttons()==(gamepad::Up|gamepad::B) && ble::home());
            disconnected(); assert(!ble::buttons() && !ble::home());
            advance(1000); assert(scans==2 && !off);
            // Actual link loss still reconnects, including a new random address.
            advertise(0x1812,1,0xab); linked(); ready(); assert(reads==2);
            report(2,1); assert(ble::buttons()==(gamepad::Right|gamepad::A));
            ble::command("ble status"); ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"disconnect_timeout")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            report(2,1,0x10);
            ble::command("ble scan"); ble::poll();
            assert(disconnects==1 && !ble::buttons() && !ble::home());
            advance(10000); assert(off==1);
            finish_restart();
            advertise(0x1812,1,0xab); linked(); ready(); report(0,2);
            assert(ble::buttons()==(gamepad::Up|gamepad::B));
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"scan_uuid")) {
            storage::config.ble_scan_uuid=0x180f;
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
            advertise(); assert(!connects);
            advertise(0x180f); assert(connects==1);
            ble::command("ble trace on"); linked(); ready(); report(2,1);
            assert(ble::buttons()==(gamepad::Right|gamepad::A));
            disconnected(); advance(1000);
            ble::command("ble target AA:BB:CC:DD:EE:FF public"); ble::poll();
            advertise(0x1812,1); assert(connects==1); // Wrong address type.
            advertise(0x1812,0,0xab); assert(connects==1); // Wrong address.
            advertise(0x1812,0); assert(connects==2); // MAC bypasses scan UUID.
            linked(); ready(); report(0,1); assert(ble::buttons()==(gamepad::Up|gamepad::A));
            ble::command("ble trace off"); ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"home")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
            advertise(); linked(); ready();
            // Home uses the fixed function bit (Button 13) in the XTARK report.
            report(255); assert(!ble::home() && !ble::buttons() && !activity);
            report(255,0,0x10); assert(ble::home() && !ble::buttons() && activity==1);
            disconnected(); advance(1000);
            advertise(); linked(); ready();
            report(255,0,0x10); assert(ble::home() && !ble::buttons() && activity==2);
            advance(60000); assert(ble::home()); // No heartbeat required even for Home.
            report(1,3,0x1c); assert(ble::home() && ble::buttons()==(gamepad::Up|gamepad::Right|15));
            // An unrelated report must not release Home; malformed selected input must.
            hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,2});
            assert(ble::home());
            hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,4});
            assert(!ble::home() && !ble::buttons());
            report(255,0,0x10); report(255); assert(!ble::home());
            report(255,0,0x10);
            hid({HCI_EVENT_GATTSERVICE_META,3,GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED,7,0});
            assert(!ble::home());
            report(255,0,0x10); disconnected(); assert(!ble::home()); advance(1000);
            // A controller that sends no Home bit remains a normal eight-key pad.
            advertise(); linked(); ready(); report(255,0,0x0c);
            assert(!ble::home() && ble::buttons()==(gamepad::Select|gamepad::Start));
            // Other physical function bits (L2/R2/L3/R3) must not return to menu.
            report(255,0,0x63); assert(!ble::home() && !ble::buttons());
            disconnected(); advance(1000); advertise(); linked(); ready();
            report(255,0,0x10); ble::command("ble off"); ble::poll(); assert(!ble::home());
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"startup_timeout")) {
            silent_power_off=true;
            advance(10000); assert(off==1); // No WORKING event on first start.
            ble::poll(); advance(1000); assert(on==2 && inits==1);
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==1);
            advertise(); linked(); ready(); assert(reads==1);
        } else {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
            if(!std::strcmp(argv[1],"request_failed")) connect_result=ERROR_CODE_COMMAND_DISALLOWED;
            advertise();
            if(!std::strcmp(argv[1],"sync_cancel")) {
                sync_cancel=true;
                // BTstack can complete cancellation inline if CREATE_CONNECTION
                // is still queued. A long service gap must not lose its retry.
                advance(59093); assert(cancels==1);
                ble::poll(); assert(off==1 && scans==1);
                finish_restart();
            } else if(!std::strcmp(argv[1],"cancel_stale_scan")) {
                // Model a controller that acknowledges cancellation but produces
                // no more advertisements until powered off/on.
                cancel_blocks_scan=true;
                advance(10000); assert(cancels==1 && scan_blocked);
                linked(ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
                // A command during pending recovery must not resume scanning early.
                ble::command("ble on"); ble::poll(); assert(off==1 && scans==1);
                unsigned previous_connects=connects; advertise(0x1812,1,0xbb);
                assert(connects==previous_connects);
                finish_restart(); assert(!scan_blocked);
            } else if(!std::strcmp(argv[1],"cancel_late_success")) {
                advance(10000); assert(cancels==1);
                // A connection may finish just as cancellation was requested.
                linked(); assert(disconnects==1 && !reads && !off);
                disconnected(); ble::poll(); assert(off==1 && scans==1);
                finish_restart();
            } else if(!std::strcmp(argv[1],"connect_failed") || !std::strcmp(argv[1],"request_failed")) {
                if(!std::strcmp(argv[1],"connect_failed")) linked(ERROR_CODE_CONNECTION_TIMEOUT);
                ble::poll(); assert(off==1 && scans==1);
                finish_restart(); connect_result=0;
            } else if(!std::strcmp(argv[1],"sync_disconnect")) {
                linked(); ready(); report(2,1); sync_disconnect=true;
                ble::command("ble scan"); ble::poll(); assert(!ble::buttons());
                advance(1000); assert(scans==2 && !off);
            } else if(!std::strcmp(argv[1],"recovery_off")) {
                advance(10000); assert(cancels==1);
                advance(10000); assert(off==1 && radio_state==HCI_STATE_HALTING);
                ble::command("ble off"); ble::poll();
                event({BTSTACK_EVENT_STATE,1,HCI_STATE_OFF}); ble::poll();
                advance(1000); assert(on==1); // Disabling cancels automatic restart.
                ble::command("ble on"); ble::poll(); assert(on==2 && inits==1);
                event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==2);
            } else {
                assert(!std::strcmp(argv[1],"lost_cancel"));
                // Model a radio poll stall, followed by a missing cancel event.
                poll_delay=59093; ble::poll(); assert(cancels==1);
                silent_power_off=true; advance(10000); assert(off==1);
                ble::poll(); advance(1000); assert(on==2 && inits==1);
                event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==2);
            }
            advertise(); linked(); ready(); assert(reads);
            report(2,1); assert(ble::buttons()==(gamepad::Right|gamepad::A));
        }
        ble::command("ble status"); ble::shutdown(); return 0;
    }
    // Exact protocol lengths, diagonal hats, eight buttons and reserved bits.
    gamepad::Receiver receiver;
    uint8_t data[]={128,128,128,128,15,0,0,0,0,0};
    assert(receiver.receive(data,10) && !receiver.buttons);
    constexpr uint8_t hats[]={gamepad::Up,gamepad::Up|gamepad::Right,gamepad::Right,gamepad::Down|gamepad::Right,
        gamepad::Down,gamepad::Down|gamepad::Left,gamepad::Left,gamepad::Up|gamepad::Left};
    for(unsigned h=0;h<8;++h) {
        data[4]=h; data[5]=3; data[6]=12;
        assert(receiver.receive(data,10) && receiver.buttons==(hats[h]|15));
    }
    receiver.release(); assert(!receiver.buttons);
    for(unsigned length=0;length<13;++length) if(length!=10) assert(!receiver.receive(data,length));
    data[5]=data[6]=0;
    for(unsigned h=0;h<256;++h) {
        data[4]=h; data[9]=0xa5; // Padding does not affect keys or direction.
        assert(receiver.receive(data,10) && receiver.buttons==((h&15)<8?hats[h&15]:0));
    }
    assert(!receiver.receive(nullptr,10));
    uint8_t address[6];
    assert(gamepad::address("Aa:BB:cc:DD:ee:FF",address) && address[0]==0xaa && address[5]==0xff);
    for(const char *s:{"","AA:BB:CC:DD:EE:FG","AA:BB:CC:DD:EE:FFx","AA-BB:CC:DD:EE:FF"})
        assert(!gamepad::address(s,address));
    uint16_t uuid=0;
    assert(gamepad::uuid16("0x1812",uuid) && uuid==0x1812);
    assert(gamepad::uuid16("0XABcd",uuid) && uuid==0xabcd);
    for(const char *s:{"","1812","0x181","0x1812x","0xG812","0x0000"})
        assert(!gamepad::uuid16(s,uuid));

    disk=true; ble::init(); ble::poll(); assert(!inits && !polls);
    disk=false; storage::config.ble_enabled=false; ble::init(); assert(!inits);
    ble::command("ble on"); ble::poll(); assert(inits==1 && on==1);
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==1);
    advance(60000); assert(scans==1 && !off); // Healthy idle scanning never restarts the controller.
    advertise(0x180f); assert(!connects); // Ignore battery-only peripherals.
    advertise(); assert(connects==1 && connected_type==BD_ADDR_TYPE_LE_RANDOM && connected_peer[0]==0xaa);
    linked(); ready(); assert(reads==1);
    report(1,3,12); assert(ble::buttons()==(gamepad::Up|gamepad::Right|15) && activity==1);
    advance(2000); assert(ble::buttons()); // Change-only HID must retain held keys.
    report(255); assert(!ble::buttons() && activity==1); // Neutral heartbeat is not activity.
    report(2,1); assert(ble::buttons()==(gamepad::Right|gamepad::A));
    // Truncated selected report releases keys, unrelated report IDs do not.
    hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,4}); assert(!ble::buttons());
    report(2,1);
    hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,2}); assert(ble::buttons());
    disconnected(); assert(!ble::buttons()); advance(1000); assert(scans==2);
    // Reject incompatible Report Maps and failed HID discovery.
    advertise(); linked(); valid_map=false; ready(); assert(disconnects==1 && !ble::buttons());
    disconnected(); advance(1000); unsigned before=connects; advertise(); assert(connects==before);
    advance(30000); advertise(); assert(connects==before+1); linked(); valid_map=true;
    ready(0x0e); assert(disconnects==2); disconnected(); advance(1000);
    // Explicit target accepts only matching address and address type.
    ble::command("ble target AA:BB:CC:DD:EE:FF public"); ble::poll();
    ble::command("ble target AA:BB:CC:DD:EE:FF random extra");
    assert(!storage::config.ble_random); // Invalid arguments preserve settings.
    before=connects; advertise(); advertise(0x1812,0,0xab); assert(connects==before);
    advertise(0x180f,0); assert(connects==before+1); linked(); ready(); report(0,1);
    assert(ble::buttons());
    ble::command("ble off"); ble::poll(); assert(!ble::buttons() && disconnects==3);
    disconnected(); ble::poll(); assert(off==1);
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_OFF});
    ble::command("ble on"); ble::poll(); assert(on==2 && inits==1); // No full stack reinit.
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
    ble::command("ble target auto"); ble::poll(); advertise();
    advance(10000); assert(cancels==1); linked(ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
    ble::poll(); finish_restart(); advertise(); linked();
    before=disconnects; advance(10000); assert(disconnects==before+1); // Discovery timeout.
    disconnected(); advance(30000); advertise();
    ble::command("ble off"); ble::poll(); before=scans;
    ble::command("ble on"); ble::poll(); assert(scans==before); // Wait for connect cancellation.
    linked(ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER); advance(1000); assert(scans==before+1);
    connect_result=ERROR_CODE_COMMAND_DISALLOWED; advertise(); before=scans;
    ble::poll(); finish_restart(); assert(scans==before+1); connect_result=0;
    ble::command("ble status"); assert(!ble::command("help"));
    ble::shutdown(); assert(deinits==1 && !ble::buttons());
}
