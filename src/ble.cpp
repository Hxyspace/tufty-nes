#include "ble.hpp"
#include "hid_gamepad.hpp"
#include "storage.hpp"
#include "power.hpp"
#include "usb.hpp"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "btstack.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ble {
namespace {
enum class State {Off,Starting,Scanning,Connecting,Discovering,Ready,Disconnecting,Backoff,Recovering,Error};
State state=State::Off;
bool initialized=false,working=false,reconfigure=false;
bool tracing=false;
bool remote_close=false;
uint32_t state_at=0,max_poll_gap=0,last_poll=0,max_poll_work=0,restarts=0;
uint32_t scan_reports=0,scan_candidates=0;
const char *restart_reason=nullptr;
hci_con_handle_t handle=HCI_CON_HANDLE_INVALID;
uint16_t cid=0;
uint8_t service_index=0,report_id=0;
bd_addr_t peer{},skipped_peer{};
bd_addr_type_t peer_type=BD_ADDR_TYPE_LE_PUBLIC,skipped_type=BD_ADDR_TYPE_UNKNOWN;
uint32_t skipped_at=0;
uint8_t descriptor_storage[1024];
hidpad::Receiver receiver;
btstack_packet_callback_registration_t hci_registration{},sm_registration{};
uint32_t now() {return to_ms_since_boot(get_absolute_time());}
const char *name() {
    switch(state) {
        case State::Off:return "OFF"; case State::Starting:return "STARTING";
        case State::Scanning:return "SCANNING"; case State::Connecting:return "CONNECTING";
        case State::Discovering:return "DISCOVERING"; case State::Ready:return "READY";
        case State::Disconnecting:return "DISCONNECTING"; case State::Backoff:return "RETRY";
        case State::Recovering:return "RECOVERING";
        case State::Error:return "ERROR";
    }
    return "ERROR";
}
void phase(State value) {
    state=value; state_at=now();
    if(value!=State::Ready) remote_close=false;
}
void trace_bytes(const uint8_t *data,size_t bytes) {
    for(size_t i=0;i<bytes;++i) std::printf("%s%02X",i?" ":"",data[i]);
    std::printf("\n");
}
void scan() {
    receiver.release();
    scan_reports=scan_candidates=0;
    gap_set_scan_parameters(1,96,48); // Active scan also receives scan responses.
    gap_start_scan(); phase(State::Scanning);
    std::printf("BLE: SCANNING HID\n");
}
void retry() {
    receiver.release(); cid=0;
    phase(State::Backoff);
}
void recover(const char *reason) {
    std::printf("BLE: RECOVER reason=%s state=%s age_ms=%lu; restarting controller\n",reason,name(),
        static_cast<unsigned long>(now()-state_at));
    receiver.release(); ++restarts; restart_reason=nullptr;
    // Keep the stack/handlers alive. BTstack's halt watchdog closes a stalled
    // transport; wait for OFF before issuing ON, without reinitializing GATT.
    phase(State::Recovering);
    gap_stop_scan();
    hci_power_control(HCI_POWER_OFF);
}
void disconnect() {
    // gap_disconnect/cancel may deliver completion synchronously. Publish the
    // waiting state first so their callback's retry() remains authoritative.
    receiver.release(); phase(State::Disconnecting);
    if(handle!=HCI_CON_HANDLE_INVALID) gap_disconnect(handle);
    else gap_connect_cancel();
}
void reject(uint8_t reason) {
    // A failed pairing also fails the pending HIDS query. Both callbacks may
    // arrive for one failure; do not restart an in-progress disconnect/retry.
    if(state!=State::Discovering && state!=State::Ready) return;
    receiver.release();
    std::memcpy(skipped_peer,peer,sizeof(peer)); skipped_type=peer_type; skipped_at=now();
    std::printf("BLE: HID FAILED status=0x%02x\n",reason);
    if(handle!=HCI_CON_HANDLE_INVALID) disconnect();
    else retry();
}
// Check the Report Map before interpreting XTARK byte offsets. In particular,
// do not reinterpret a keyboard/mouse/consumer report as the XTARK gamepad.
bool button_layout(const uint8_t *map,uint16_t bytes,uint16_t id) {
    uint16_t fields=0;
    btstack_hid_usage_iterator_t iterator;
    btstack_hid_usage_iterator_init(&iterator,map,bytes,HID_REPORT_TYPE_INPUT);
    while(btstack_hid_usage_iterator_has_more(&iterator)) {
        btstack_hid_usage_item_t item;
        btstack_hid_usage_iterator_get_item(&iterator,&item);
        if(item.report_id!=id) continue;
        // The tenth byte is constant padding in the physical XTARK descriptor.
        if(item.bit_pos+item.size>72) return false;
        if(item.usage_page!=9 || item.size!=1 || (item.descriptor_item.item_value&7)!=2) continue;
        if(item.usage>=1 && item.usage<=16 && item.bit_pos==39+item.usage)
            fields|=uint16_t(1u<<(item.usage-1));
    }
    return fields==0xffff;
}
bool select_layout(uint8_t instances) {
    for(uint8_t index=0;index<instances;++index) {
        const uint8_t *map=hids_host_descriptor_storage_get_descriptor_data(cid,index);
        uint16_t bytes=hids_host_descriptor_storage_get_descriptor_len(cid,index);
        if(tracing) {
            std::printf("BLE: REPORT MAP service=%u bytes=%u data=",index,bytes);
            trace_bytes(map,map?bytes:0);
        }
        if(!map || !bytes) continue;
        btstack_hid_usage_iterator_t iterator;
        btstack_hid_usage_iterator_init(&iterator,map,bytes,HID_REPORT_TYPE_INPUT);
        while(btstack_hid_usage_iterator_has_more(&iterator)) {
            btstack_hid_usage_item_t item;
            btstack_hid_usage_iterator_get_item(&iterator,&item);
            if(item.usage_page!=1 || item.usage!=0x39 || item.bit_pos!=32 ||
               item.size!=4 || item.global_logical_minimum!=0 ||
               iterator.global_logical_maximum!=7 || (item.descriptor_item.item_value&0x47)!=0x42) continue;
            if(btstack_hid_get_report_size_for_id(item.report_id,HID_REPORT_TYPE_INPUT,map,bytes)!=hidpad::ReportBytes ||
               !button_layout(map,bytes,item.report_id)) continue;
            service_index=index;
            report_id=item.report_id==HID_REPORT_ID_UNDEFINED?0:uint8_t(item.report_id);
            return true;
        }
    }
    std::printf("BLE: INCOMPATIBLE REPORT MAP; use ble trace on, then ble scan\n");
    return false;
}
void hid_event(uint8_t packet_type,uint16_t,uint8_t *packet,uint16_t size) {
    if((packet_type!=HCI_EVENT_PACKET && packet_type!=HCI_EVENT_GATTSERVICE_META) ||
       size<5 || packet[0]!=HCI_EVENT_GATTSERVICE_META || little_endian_read_16(packet,3)!=cid) return;
    switch(packet[2]) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            if(size<8 || state!=State::Discovering) return;
            uint8_t status=gattservice_subevent_hid_service_connected_get_status(packet);
            if(status || !select_layout(gattservice_subevent_hid_service_connected_get_num_instances(packet))) {
                reject(status?status:ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE); return;
            }
            phase(State::Ready); receiver.release();
            std::printf("BLE: READY peer=%s report=%u\n",bd_addr_to_str(peer),report_id);
            // Fetch held state even from physical controllers that only notify changes.
            hids_host_send_get_report(cid,report_id,HID_REPORT_TYPE_INPUT);
            break;
        }
        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            if(state!=State::Ready || size<7 || packet[5]!=service_index || packet[6]!=report_id) return;
            if(size<9) {receiver.receive(nullptr,0); return;}
            uint16_t bytes=gattservice_subevent_hid_report_get_report_len(packet);
            const uint8_t *report=gattservice_subevent_hid_report_get_report(packet);
            // BTstack prepends the Report ID, although GATT notifications do not.
            if(bytes!=hidpad::ReportBytes+1 || size<9+bytes || report[0]!=report_id) {
                receiver.receive(nullptr,0); return;
            }
            if(remote_close) return;
            if(hidpad::disconnect_report(report+1,bytes-1)) {
                receiver.release(); remote_close=true;
                std::printf("BLE: REMOTE CLOSE; disconnecting gamepad\n");
                return;
            }
            if(receiver.receive(report+1,bytes-1,now())) {
                if(receiver.buttons || receiver.home) power::activity();
            }
            break;
        }
        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
            receiver.release();
            break;
        default:break;
    }
}
void event(uint8_t packet_type,uint16_t,uint8_t *packet,uint16_t size) {
    if(packet_type!=HCI_EVENT_PACKET || size<2) return;
    switch(packet[0]) {
        case BTSTACK_EVENT_STATE:
            if(size<3) return;
            working=btstack_event_state_get_state(packet)==HCI_STATE_WORKING;
            if(!working) receiver.release();
            if(btstack_event_state_get_state(packet)==HCI_STATE_OFF) {
                handle=HCI_CON_HANDLE_INVALID; cid=0;
            }
            if(working && storage::config.ble_enabled && state==State::Starting) scan();
            break;
        case GAP_EVENT_ADVERTISING_REPORT: {
            if(state!=State::Scanning || size<12 || size<12+gap_event_advertising_report_get_data_length(packet)) return;
            ++scan_reports;
            uint8_t kind=gap_event_advertising_report_get_advertising_event_type(packet);
            if(kind!=0 && kind!=4) return; // Connectable ADV_IND or scan response.
            const uint8_t *data=gap_event_advertising_report_get_data(packet);
            uint8_t bytes=gap_event_advertising_report_get_data_length(packet);
            if(!storage::config.ble_peer[0] && !ad_data_contains_uuid16(bytes,data,storage::config.ble_scan_uuid)) return;
            bd_addr_t address;
            gap_event_advertising_report_get_address(packet,address);
            bd_addr_type_t type=static_cast<bd_addr_type_t>(gap_event_advertising_report_get_address_type(packet));
            if(storage::config.ble_peer[0]) {
                bd_addr_t target;
                if(!hidpad::address(storage::config.ble_peer,target) || std::memcmp(target,address,6) ||
                   storage::config.ble_random!=(type==BD_ADDR_TYPE_LE_RANDOM)) return;
            }
            if(type==skipped_type && !std::memcmp(address,skipped_peer,6) && now()-skipped_at<30000) return;
            ++scan_candidates;
            gap_stop_scan(); std::memcpy(peer,address,6); peer_type=type;
            std::printf("BLE: CONNECTING peer=%s type=%s\n",bd_addr_to_str(peer),type==BD_ADDR_TYPE_LE_RANDOM?"random":"public");
            phase(State::Connecting);
            uint8_t status=gap_connect(peer,peer_type);
            if(status) {
                std::printf("BLE: CONNECT REQUEST FAILED status=0x%02x\n",status);
                restart_reason="connection request failed";
                retry();
            }
            break;
        }
        case HCI_EVENT_META_GAP: {
            if(size<4 || packet[2]!=GAP_SUBEVENT_LE_CONNECTION_COMPLETE) return;
            uint8_t status=gap_subevent_le_connection_complete_get_status(packet);
            if(status) {
                if(state==State::Connecting || (state==State::Disconnecting && handle==HCI_CON_HANDLE_INVALID)) {
                    bool cancelled=state==State::Disconnecting && status==ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
                    std::printf("BLE: CONNECT %s status=0x%02x\n",cancelled?"CANCELLED":"FAILED",status);
                    if(!cancelled && !restart_reason) restart_reason="connection failed";
                    retry();
                }
                return;
            }
            if(size<12) return;
            handle=gap_subevent_le_connection_complete_get_connection_handle(packet);
            if(state==State::Recovering) {gap_disconnect(handle); return;}
            if(state!=State::Connecting || !storage::config.ble_enabled) {disconnect(); return;}
            phase(State::Discovering);
            status=hids_host_connect(handle,hid_event,HID_PROTOCOL_MODE_REPORT,&cid);
            if(status) reject(status);
            break;
        }
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if(size<6 || hci_event_disconnection_complete_get_connection_handle(packet)!=handle) return;
            std::printf("BLE: DISCONNECTED reason=0x%02x\n",hci_event_disconnection_complete_get_reason(packet));
            handle=HCI_CON_HANDLE_INVALID;
            if(state!=State::Recovering) retry();
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            if(size>=4 && sm_event_just_works_request_get_handle(packet)==handle)
                sm_just_works_confirm(handle);
            break;
        case SM_EVENT_PAIRING_COMPLETE: {
            if(size<13 || sm_event_pairing_complete_get_handle(packet)!=handle) return;
            uint8_t status=sm_event_pairing_complete_get_status(packet);
            uint8_t reason=sm_event_pairing_complete_get_reason(packet);
            if(status || tracing)
                std::printf("BLE: PAIRING %s status=0x%02x reason=0x%02x\n",status?"FAILED":"OK",status,reason);
            if(status) reject(status);
            break;
        }
        default:break;
    }
}
void report() {
    std::printf("BLE STATUS: state=%s buttons=0x%02x home=%u reports=%lu rejected=%lu poll_gap_ms=%lu poll_work_ms=%lu restarts=%lu target=%s type=%s\n",
        name(),receiver.buttons,receiver.home,
        static_cast<unsigned long>(receiver.received),static_cast<unsigned long>(receiver.rejected),
        static_cast<unsigned long>(max_poll_gap),static_cast<unsigned long>(max_poll_work),
        static_cast<unsigned long>(restarts),storage::config.ble_peer[0]?storage::config.ble_peer:"auto",
        storage::config.ble_random?"random":"public");
    std::printf("BLE PEER: address=%s type=%s service=%u report=%u scan_uuid=0x%04X trace=%u\n",
        handle==HCI_CON_HANDLE_INVALID?"none":bd_addr_to_str(peer),
        peer_type==BD_ADDR_TYPE_LE_RANDOM?"random":"public",service_index,report_id,storage::config.ble_scan_uuid,tracing);
    std::printf("BLE SCAN: reports=%lu candidates=%lu\n",static_cast<unsigned long>(scan_reports),
        static_cast<unsigned long>(scan_candidates));
}
}
uint8_t buttons() {return receiver.buttons;}
bool home() {return receiver.home;}
void init() {
    if(initialized || !storage::config.ble_enabled || usb::disk_mode()) return;
    phase(State::Starting);
    int result=cyw43_arch_init();
    if(result) {phase(State::Error); std::printf("BLE: INIT FAILED rc=%d\n",result); return;}
    initialized=true;
    l2cap_init(); sm_init(); gatt_client_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    // BTstack defaults to SC-only when ENABLE_LE_SECURE_CONNECTIONS is built.
    // Setting AuthReq=0 alone does not disable that policy. Match the XTARK
    // receiver's legacy Just Works pairing, without requiring bonding or MITM.
    sm_set_secure_connections_only_mode(false);
    sm_set_authentication_requirements(0); // Temporary pairing, without bonding.
    hids_host_init(descriptor_storage,sizeof(descriptor_storage));
    hci_registration.callback=event; hci_add_event_handler(&hci_registration);
    sm_registration.callback=event; sm_add_event_handler(&sm_registration);
    gap_set_connection_parameters(48,48,6,12,0,100,0,0);
    last_poll=now(); phase(State::Starting);
    hci_power_control(HCI_POWER_ON);
}
void poll() {
    if(usb::disk_mode()) return;
    if(reconfigure) {
        reconfigure=false; receiver.release(); skipped_type=BD_ADDR_TYPE_UNKNOWN;
        if(initialized) {
            if(state==State::Scanning) gap_stop_scan();
            if(state==State::Disconnecting || state==State::Recovering || restart_reason) {} // Apply after completion.
            else if(state==State::Connecting || handle!=HCI_CON_HANDLE_INVALID) disconnect();
            else if(storage::config.ble_enabled) {
                if(working) scan(); else {phase(State::Starting); hci_power_control(HCI_POWER_ON);}
            } else {phase(State::Off); hci_power_control(HCI_POWER_OFF);}
        } else if(storage::config.ble_enabled) init();
    }
    if(!initialized) return;
    uint32_t at=now();
    if(at-last_poll>max_poll_gap) max_poll_gap=at-last_poll;
    last_poll=at;
    cyw43_arch_poll();
    // Finish HIDS callbacks before terminating the link, and never persistently
    // disable the radio for an Android app-close message.
    if(remote_close && state==State::Ready) disconnect();
    if(state==State::Ready && receiver.poll(now())) {
        power::activity();
    }
    uint32_t work=now()-at;
    if(work>max_poll_work) max_poll_work=work;
    if(work>=1000) std::printf("BLE: SLOW POLL work_ms=%lu\n",static_cast<unsigned long>(work));
    uint32_t elapsed=now()-state_at;
    if(state==State::Backoff && restart_reason) {
        // Cancellation completion only confirms the initiating procedure ended.
        // After a failed/timed-out attempt, reset the controller before scanning
        // again, matching the off/on recovery without cycling a healthy link.
        // Run outside the event callback so BTstack can finish freeing clients.
        recover(restart_reason);
    } else if(state==State::Recovering && hci_get_state()==HCI_STATE_OFF) {
        working=false; handle=HCI_CON_HANDLE_INVALID; retry();
    } else if(state==State::Backoff && (!storage::config.ble_enabled || elapsed>=1000)) {
        if(storage::config.ble_enabled) {
            if(working) scan();
            else {phase(State::Starting); hci_power_control(HCI_POWER_ON);}
        }
        else if(!storage::config.ble_enabled) {phase(State::Off); hci_power_control(HCI_POWER_OFF);}
    } else if(state==State::Connecting && elapsed>=10000) {
        std::printf("BLE: CONNECT TIMEOUT; cancelling before controller restart\n");
        restart_reason="connection timeout";
        disconnect();
    } else if(state==State::Discovering && elapsed>=10000) reject(ERROR_CODE_CONNECTION_TIMEOUT);
    else if((state==State::Starting || state==State::Disconnecting) && elapsed>=10000) {
        recover(restart_reason?restart_reason:"state timeout");
    }
}
void shutdown() {
    receiver.release(); reconfigure=false;
    if(initialized) {
        if(state==State::Scanning) gap_stop_scan();
        if(state==State::Connecting) gap_connect_cancel();
        hci_power_control(HCI_POWER_OFF);
        cyw43_arch_deinit(); initialized=false;
    }
    working=false; restart_reason=nullptr; phase(State::Off);
}
bool command(const char *line) {
    if(!std::strcmp(line,"ble status")) report();
    else if(!std::strcmp(line,"ble trace on") || !std::strcmp(line,"ble trace off")) {
        tracing=!std::strcmp(line,"ble trace on");
        std::printf("BLE: TRACE %s\n",tracing?"ON":"OFF");
    }
    else if(!std::strcmp(line,"ble on") || !std::strcmp(line,"ble off")) {
        storage::config.ble_enabled=!std::strcmp(line,"ble on");
        usb::dirty=storage::state_ready(); reconfigure=true;
        std::printf("BLE: %s REQUESTED; use save to persist\n",storage::config.ble_enabled?"ON":"OFF");
    } else if(!std::strcmp(line,"ble scan")) {
        storage::config.ble_enabled=true; usb::dirty=storage::state_ready(); reconfigure=true;
        std::printf("BLE: SCAN REQUESTED\n");
    } else if(!std::strcmp(line,"ble target auto")) {
        storage::config.ble_peer[0]=0; storage::config.ble_random=false;
        usb::dirty=storage::state_ready(); reconfigure=true;
        std::printf("BLE: TARGET auto; use save to persist\n");
    } else if(!std::strncmp(line,"ble target ",11)) {
        char address[18],type[7],tail; uint8_t bytes[6];
        if(std::sscanf(line+11,"%17s %6s %c",address,type,&tail)!=2 || !hidpad::address(address,bytes) ||
           (std::strcmp(type,"public") && std::strcmp(type,"random"))) {
            std::printf("BLE: INVALID_TARGET; ble target AA:BB:CC:DD:EE:FF public|random\n");
        } else {
            std::memcpy(storage::config.ble_peer,address,18); storage::config.ble_random=!std::strcmp(type,"random");
            usb::dirty=storage::state_ready(); reconfigure=true;
            std::printf("BLE: TARGET %s %s; use save to persist\n",address,type);
        }
    } else if(!std::strncmp(line,"ble",3)) {
        std::printf("ble status | ble on | ble off | ble scan | ble target auto | ble target MAC public|random | ble trace on|off\n");
    } else return false;
    return true;
}
}
