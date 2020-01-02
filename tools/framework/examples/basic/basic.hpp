#ifndef __BASIC_HPP__
#define __BASIC_HPP__

#include "platform.hpp"

namespace tai::basic {

    const uint8_t BASIC_NUM_MODULE = 4;
    const uint8_t BASIC_NUM_NETIF = 1;
    const uint8_t BASIC_NUM_HOSTIF = 2;

    // the same object ID format as examples/stub is used
    const uint8_t OBJECT_TYPE_SHIFT = 48;

    class Platform : public tai::Platform {
        public:
            Platform(const tai_service_method_table_t * services);
            tai_status_t create(tai_object_type_t type, tai_object_id_t module_id, uint32_t attr_count, const tai_attribute_t * const attr_list, tai_object_id_t *id);
            tai_status_t remove(tai_object_id_t id) {
                return TAI_STATUS_NOT_SUPPORTED;
            }
            tai_object_type_t get_object_type(tai_object_id_t id);
            tai_object_id_t   get_module_id(tai_object_id_t id);
    };

    class Module;
    class NetIf;
    class HostIf;

    using S_Module = std::shared_ptr<Module>;
    using S_NetIf  = std::shared_ptr<NetIf>;
    using S_HostIf = std::shared_ptr<HostIf>;

    // The FSM for state handling of the hardware
    //
    // It is not mandatory to implement FSM to use the TAI library framework.
    // You can see that examples/stub doesn't implement it.
    //
    // The TAI library framework defines 4 FSM states, INIT, WAITING_CONFIGURATION, READY and END.
    // The FSM starts with INIT and stops when it reaches to END
    // The TAI library framework doesn't have any assumption on how to transit between these states.
    //
    // You need to implement FSM::cb(FSMState state) which returns fsm_callback for handling every states other than END.
    // ( If FSM::cb(FSMState state) returns nullptr, the state goes to END )
    // The callback returns the next state which the FSM transit.
    //
    // When the framework want to transit to another state (described in basic.cpp when this happens),
    // the framework triggers an event. This event can be captured through eventfd.
    // `int tai::FSM::get_event_fd()` returns the event fd and `FSMState tai::FSM::next_state()` returns
    // the next state which the framework is requesting to transite.
    //
    // In typical case, the callback respects what the framework is requesting, and return the next state promptly.
    //
    // If needed, you can define additional states and implement fsm_callbacks for them.
    //
    // You can implement FSM::state_change_cb() which returns fsm_state_change_callback.
    // This callback will get called everytime when the FSM state changes.
    //
    // In this example, a FSM is created per module and shared among module and its netif/hostif.
    // A FSM is fed to a constructor of Object.
    //
    // set_module/set_netif/set_hostif are implemented to enable the access to TAI objects from FSM
    // it is not mandatory and required by the framework, but you will find it necessary most of the time to do meaningful stuff
    class FSM : public tai::FSM {
        // requirements to inherit tai::FSM
        public:
            bool configured();
        private:
            fsm_state_change_callback state_change_cb();
            fsm_callback cb(FSMState state);

        // methods/fields specific to this example
        public:
            FSM() : m_module(nullptr), m_netif(nullptr), m_hostif{} {}
            int set_module(S_Module module);
            int set_netif(S_NetIf   netif);
            int set_hostif(S_HostIf hostif, int index);

            tai_status_t set_tx_dis(const tai_attribute_t* const attribute);
            tai_status_t get_tx_dis(tai_attribute_t* const attribute);

        private:
            FSMState _state_change_cb(FSMState current, FSMState next, void* user);

            FSMState _init_cb(FSMState current, void* user);
            FSMState _waiting_configuration_cb(FSMState current, void* user);
            FSMState _ready_cb(FSMState current, void* user);

            S_Module m_module;
            S_NetIf m_netif;
            S_HostIf m_hostif[BASIC_NUM_HOSTIF];
    };

    using S_FSM = std::shared_ptr<FSM>;

    class Module : public tai::Object<TAI_OBJECT_TYPE_MODULE> {
        public:
            // 4th argument to the Object constructor is a user context which is passed in getter()/setter() callbacks
            // getter()/setter() callbacks is explained in basic.hpp
            Module(uint32_t count, const tai_attribute_t *list, S_FSM fsm) : m_fsm(fsm), Object(count, list, fsm, reinterpret_cast<void*>(fsm.get())) {
                std::string loc;
                for ( auto i = 0; i < count; i++ ) {
                    if ( list[i].id == TAI_MODULE_ATTR_LOCATION ) {
                        loc = std::string(list[i].value.charlist.list, list[i].value.charlist.count);
                        break;
                    }
                }
                if ( loc == "" ) {
                    throw Exception(TAI_STATUS_MANDATORY_ATTRIBUTE_MISSING);
                }
                auto i = std::stoi(loc);
                m_id = static_cast<tai_object_id_t>(uint64_t(TAI_OBJECT_TYPE_MODULE) << OBJECT_TYPE_SHIFT | i);
            }
            tai_object_id_t id() {
                return m_id;
            }
            S_FSM fsm() {
                return m_fsm;
            }
        private:
            tai_object_id_t m_id;
            S_FSM m_fsm;
    };


    class NetIf : public tai::Object<TAI_OBJECT_TYPE_NETWORKIF> {
        public:
            NetIf(S_Module module, uint32_t count, const tai_attribute_t *list) : Object(count, list, module->fsm(), reinterpret_cast<void*>(module->fsm().get())) {
                int index = -1;
                for ( auto i = 0; i < count; i++ ) {
                    if ( list[i].id == TAI_NETWORK_INTERFACE_ATTR_INDEX ) {
                        index = list[i].value.u32;
                        break;
                    }
                }
                if ( index < 0 ) {
                    throw Exception(TAI_STATUS_MANDATORY_ATTRIBUTE_MISSING);
                }
                m_id = static_cast<tai_object_id_t>(uint64_t(TAI_OBJECT_TYPE_NETWORKIF) << OBJECT_TYPE_SHIFT | (module->id() & 0xff) << 8 | index);
            }
            tai_object_id_t id() {
                return m_id;
            }
        private:
            tai_object_id_t m_id;
    };

    class HostIf : public tai::Object<TAI_OBJECT_TYPE_HOSTIF> {
        public:
            HostIf(S_Module module, uint32_t count, const tai_attribute_t *list) : Object(count, list, module->fsm(), reinterpret_cast<void*>(module->fsm().get())) {
                int index = -1;
                for ( auto i = 0; i < count; i++ ) {
                    if ( list[i].id == TAI_HOST_INTERFACE_ATTR_INDEX ) {
                        index = list[i].value.u32;
                        break;
                    }
                }
                if ( index < 0 ) {
                    throw Exception(TAI_STATUS_MANDATORY_ATTRIBUTE_MISSING);
                }
                m_id = static_cast<tai_object_id_t>(uint64_t(TAI_OBJECT_TYPE_HOSTIF) << OBJECT_TYPE_SHIFT | (module->id() & 0xff) << 8 | index);
            }
            tai_object_id_t id() {
                return m_id;
            }
        private:
            tai_object_id_t m_id;
    };

};

#ifdef TAI_EXPOSE_PLATFORM
using tai::basic::Platform;
#endif

#endif // __BASIC_HPP__
