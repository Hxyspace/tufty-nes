// Production BLE adapter and real BTstack HID parser, with radio/HIDS endpoints
// mocked at their public APIs. No controller or hardware is needed.
#include "ble.hpp"
#include "hid_gamepad.hpp"
#include "storage.hpp"
#include "btstack.h"
#include "pico/stdlib.h"
#include <algorithm>
#include <cassert>
#include <cstring>
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
btstack_packet_handler_t hci_handler=nullptr,hid_handler=nullptr;
bd_addr_t connected_peer{};
bd_addr_type_t connected_type=BD_ADDR_TYPE_UNKNOWN;
constexpr uint8_t map[]={
    0x05,0x01,0x09,0x05,0xa1,0x01,0x85,0x01,
    0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x04,
    0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x81,0x03,
    0x25,0x07,0x95,0x01,0x09,0x39,0x81,0x42,
    0x05,0x09,0x25,0x01,0x75,0x01,0x95,0x02,
    0x09,0x01,0x09,0x02,0x81,0x02,
    0x95,0x06,0x81,0x03,0x95,0x02,0x81,0x03,
    0x09,0x09,0x09,0x0a,0x81,0x02,0x95,0x04,0x81,0x03,
    0x75,0x08,0x95,0x02,0x81,0x03,0xc0};
constexpr uint8_t keyboard_map[]={0x05,0x01,0x09,0x06,0xa1,0x01,
    0x75,0x08,0x95,0x09,0x09,0x30,0x81,0x02,0xc0};
std::vector<uint8_t> optional_home_map;
void home_map() {
    optional_home_map.assign(std::begin(map),std::end(map)-17);
    // Android's XTARK layout: Button 9/10/13 are Select/Start/Home.
    optional_home_map.insert(optional_home_map.end(),{
        0x95,0x03,0x09,0x09,0x09,0x0a,0x09,0x0d,0x81,0x02,
        0x95,0x03,0x81,0x03,
        0x75,0x08,0x95,0x02,0x81,0x03,0xc0});
}
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
void report(uint8_t hat,uint8_t ab=0,uint8_t menu=0) {
    hid({HCI_EVENT_GATTSERVICE_META,17,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,1,10,0,
         1,0x80,0x80,0x80,0x80,hat,ab,menu,0,0});
}
void disconnected() {event({HCI_EVENT_DISCONNECTION_COMPLETE,4,0,0x40,0,0x13});}
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
void sm_add_event_handler(btstack_packet_callback_registration_t *) {}
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
uint8_t hids_host_send_get_report(uint16_t,uint8_t,hid_report_type_t) {++reads; return 0;}
const uint8_t *hids_host_descriptor_storage_get_descriptor_data(uint16_t,uint8_t) {
    return !valid_map?keyboard_map:optional_home_map.empty()?map:optional_home_map.data();
}
uint16_t hids_host_descriptor_storage_get_descriptor_len(uint16_t,uint8_t) {
    return !valid_map?sizeof(keyboard_map):optional_home_map.empty()?sizeof(map):optional_home_map.size();
}
}
int main(int argc,char **argv) {
    if(argc==2) {
        ble::init(); assert(inits==1 && on==1);
        if(!std::strcmp(argv[1],"idle_link")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); advertise(); linked(); ready();
            assert(reads==1);
            report(2,1,0x10); unsigned held_activity=activity;
            // Simulated notification silence must not expire a held HID state.
            for(unsigned i=0;i<600;++i) advance(1000);
            assert(ble::buttons()==(tgp::Right|tgp::A) && ble::home() && activity==held_activity);
            assert(!disconnects && !off && scans==1 && reads==1);
            report(255); unsigned idle_activity=activity;
            // No app-close or service-loss probe runs after HID is READY.
            for(unsigned i=0;i<600;++i) advance(1000);
            assert(!ble::buttons() && !ble::home() && activity==idle_activity);
            assert(!disconnects && !off && scans==1 && reads==1);
            report(0,2,0x10);
            // A disconnection for another handle cannot release this controller.
            event({HCI_EVENT_DISCONNECTION_COMPLETE,4,0,0x41,0,0x13});
            assert(ble::buttons()==(tgp::Up|tgp::B) && ble::home());
            disconnected(); assert(!ble::buttons() && !ble::home());
            advance(1000); assert(scans==2 && !off);
            // Actual link loss still reconnects, including a new random address.
            advertise(0x1812,1,0xab); linked(); ready(); assert(reads==2);
            report(2,1); assert(ble::buttons()==(tgp::Right|tgp::A));
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
            assert(ble::buttons()==(tgp::Up|tgp::B));
            ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"scan_uuid")) {
            storage::config.ble_scan_uuid=0x180f;
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
            advertise(); assert(!connects);
            advertise(0x180f); assert(connects==1);
            ble::command("ble trace on"); linked(); ready(); report(2,1);
            assert(ble::buttons()==(tgp::Right|tgp::A));
            disconnected(); advance(1000);
            ble::command("ble target AA:BB:CC:DD:EE:FF public"); ble::poll();
            advertise(0x1812,1); assert(connects==1); // Wrong address type.
            advertise(0x1812,0,0xab); assert(connects==1); // Wrong address.
            advertise(0x1812,0); assert(connects==2); // MAC bypasses scan UUID.
            linked(); ready(); report(0,1); assert(ble::buttons()==(tgp::Up|tgp::A));
            ble::command("ble trace off"); ble::shutdown(); return 0;
        }
        if(!std::strcmp(argv[1],"home")) {
            event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING});
            advertise(); linked(); ready();
            // Fixed XTARK function bits work even if Home has no named usage.
            report(255); assert(!ble::home() && !ble::buttons() && !activity);
            report(255,0,0x10); assert(ble::home() && !ble::buttons() && activity==1);
            disconnected(); advance(1000);
            home_map(); advertise(); linked(); ready();
            report(255,0,0x10); assert(ble::home() && !ble::buttons() && activity==2);
            advance(60000); assert(ble::home()); // No heartbeat required even for Home.
            report(1,3,0x1c); assert(ble::home() && ble::buttons()==(tgp::Up|tgp::Right|15));
            // An unrelated report must not release Home; malformed selected input must.
            hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,2});
            assert(ble::home());
            hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,1});
            assert(!ble::home() && !ble::buttons());
            report(255,0,0x10); report(255); assert(!ble::home());
            report(255,0,0x10);
            hid({HCI_EVENT_GATTSERVICE_META,3,GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED,7,0});
            assert(!ble::home());
            report(255,0,0x10); disconnected(); assert(!ble::home()); advance(1000);
            // A controller that sends no Home bit remains a normal eight-key pad.
            optional_home_map.clear(); advertise(); linked(); ready(); report(255,0,0x0c);
            assert(!ble::home() && ble::buttons()==(tgp::Select|tgp::Start));
            // Other physical function bits (L2/R2/L3/R3) must not return to menu.
            report(255,0,0x63); assert(!ble::home() && !ble::buttons());
            disconnected(); advance(1000); home_map(); advertise(); linked(); ready();
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
            report(2,1); assert(ble::buttons()==(tgp::Right|tgp::A));
        }
        ble::command("ble status"); ble::shutdown(); return 0;
    }
    // Exact protocol lengths, diagonal hats, eight buttons and reserved bits.
    hidpad::Receiver receiver;
    uint8_t data[]={128,128,128,128,255,0,0,0,0};
    assert(receiver.receive(data,9) && !receiver.buttons);
    constexpr uint8_t hats[]={tgp::Up,tgp::Up|tgp::Right,tgp::Right,tgp::Down|tgp::Right,
        tgp::Down,tgp::Down|tgp::Left,tgp::Left,tgp::Up|tgp::Left};
    for(unsigned h=0;h<8;++h) {
        data[4]=h; data[5]=3; data[6]=12;
        assert(receiver.receive(data,9) && receiver.buttons==(hats[h]|15));
    }
    receiver.release(); assert(!receiver.buttons);
    for(unsigned length=0;length<12;++length) if(length!=9) assert(!receiver.receive(data,length));
    for(unsigned h=8;h<255;++h) {data[4]=h; assert(!receiver.receive(data,9) && !receiver.buttons);}
    assert(!receiver.receive(nullptr,9));
    uint8_t address[6];
    assert(hidpad::address("Aa:BB:cc:DD:ee:FF",address) && address[0]==0xaa && address[5]==0xff);
    for(const char *s:{"","AA:BB:CC:DD:EE:FG","AA:BB:CC:DD:EE:FFx","AA-BB:CC:DD:EE:FF"})
        assert(!hidpad::address(s,address));
    uint16_t uuid=0;
    assert(hidpad::uuid16("0x1812",uuid) && uuid==0x1812);
    assert(hidpad::uuid16("0XABcd",uuid) && uuid==0xabcd);
    for(const char *s:{"","1812","0x181","0x1812x","0xG812","0x0000"})
        assert(!hidpad::uuid16(s,uuid));

    disk=true; ble::init(); ble::poll(); assert(!inits && !polls);
    disk=false; storage::config.ble_enabled=false; ble::init(); assert(!inits);
    ble::command("ble on"); ble::poll(); assert(inits==1 && on==1);
    event({BTSTACK_EVENT_STATE,1,HCI_STATE_WORKING}); assert(scans==1);
    advance(60000); assert(scans==1 && !off); // Healthy idle scanning never restarts the controller.
    advertise(0x180f); assert(!connects); // Ignore battery-only peripherals.
    advertise(); assert(connects==1 && connected_type==BD_ADDR_TYPE_LE_RANDOM && connected_peer[0]==0xaa);
    linked(); ready(); assert(reads==1);
    report(1,3,12); assert(ble::buttons()==(tgp::Up|tgp::Right|15) && activity==1);
    advance(2000); assert(ble::buttons()); // Change-only HID must retain held keys.
    report(255); assert(!ble::buttons() && activity==1); // Neutral heartbeat is not activity.
    report(2,1); assert(ble::buttons()==(tgp::Right|tgp::A));
    // Truncated selected report releases keys, unrelated report IDs do not.
    hid({HCI_EVENT_GATTSERVICE_META,5,GATTSERVICE_SUBEVENT_HID_REPORT,7,0,0,1}); assert(!ble::buttons());
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
