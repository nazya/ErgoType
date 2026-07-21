# Keep each additional pinned-core delta as one unique, SHA-guarded replacement.
# The generated source retains the exact upstream statement beside every local
# replacement, following usb_host/upstream-porting-rules.md.
macro(ergotype_tinyusb_usbh_replace_unique LABEL UPSTREAM_VAR PORT_VAR)
    string(FIND "${TINYUSB_USBH_PORT_CONTENT}" "${${UPSTREAM_VAR}}"
           TINYUSB_USBH_LOCAL_OFFSET)
    if(TINYUSB_USBH_LOCAL_OFFSET EQUAL -1)
        message(FATAL_ERROR
                "Pinned TinyUSB usbh.c ${LABEL} anchor changed; re-audit the port")
    endif()
    string(LENGTH "${${UPSTREAM_VAR}}" TINYUSB_USBH_LOCAL_LENGTH)
    math(EXPR TINYUSB_USBH_LOCAL_TAIL_OFFSET
         "${TINYUSB_USBH_LOCAL_OFFSET} + ${TINYUSB_USBH_LOCAL_LENGTH}")
    string(SUBSTRING "${TINYUSB_USBH_PORT_CONTENT}"
           ${TINYUSB_USBH_LOCAL_TAIL_OFFSET} -1 TINYUSB_USBH_LOCAL_TAIL)
    string(FIND "${TINYUSB_USBH_LOCAL_TAIL}" "${${UPSTREAM_VAR}}"
           TINYUSB_USBH_LOCAL_SECOND)
    if(NOT TINYUSB_USBH_LOCAL_SECOND EQUAL -1)
        message(FATAL_ERROR "TinyUSB ${LABEL} anchor is not unique")
    endif()
    string(REPLACE "${${UPSTREAM_VAR}}" "${${PORT_VAR}}"
           TINYUSB_USBH_PORT_CONTENT "${TINYUSB_USBH_PORT_CONTENT}")
endmacro()

set(TINYUSB_USBH_CONTROL_IDLE_UPSTREAM [=[
TU_ATTR_ALWAYS_INLINE static inline void _set_control_xfer_stage(uint8_t stage) {
  (void) osal_mutex_lock(_usbh_mutex, OSAL_TIMEOUT_WAIT_FOREVER);
  _ctrl_xfer.stage = stage;
  (void) osal_mutex_unlock(_usbh_mutex);
}
]=])
set(TINYUSB_USBH_CONTROL_IDLE_PORT [=[
TU_ATTR_ALWAYS_INLINE static inline void _set_control_xfer_stage(uint8_t stage) {
  // Upstream TinyUSB:
  // (void) osal_mutex_lock(_usbh_mutex, OSAL_TIMEOUT_WAIT_FOREVER);
  // _ctrl_xfer.stage = stage;
  // (void) osal_mutex_unlock(_usbh_mutex);
  // Reset work cannot inspect TinyUSB's private global owner; publish only the
  // non-IDLE -> IDLE edge after dropping TinyUSB's mutex.
  bool publish_idle;

  (void) osal_mutex_lock(_usbh_mutex, OSAL_TIMEOUT_WAIT_FOREVER);
  publish_idle = stage == CONTROL_STAGE_IDLE &&
                 _ctrl_xfer.stage != CONTROL_STAGE_IDLE;
  _ctrl_xfer.stage = stage;
  (void) osal_mutex_unlock(_usbh_mutex);

  if (publish_idle) tuh_port_control_idle_cb();
}
]=])
ergotype_tinyusb_usbh_replace_unique("global control idle publication"
    TINYUSB_USBH_CONTROL_IDLE_UPSTREAM TINYUSB_USBH_CONTROL_IDLE_PORT)

set(TINYUSB_USBH_ENUM_STATE_UPSTREAM [=[
static usbh_dev0_t _dev0;

// all devices excluding zero-address
]=])
set(TINYUSB_USBH_ENUM_STATE_PORT [=[
static usbh_dev0_t _dev0;

// Upstream TinyUSB: no equivalent; the pinned core only has one boolean.
// Keep the exact provisional address and physical-retirement phase in the
// TinyUSB host owner so neither lifecycle nor another core reads private state.
enum {
  USBH_PORT_ENUM_WATCHDOG_MS = 6000,
  USBH_PORT_ENUM_DRAIN_MS = 2,
  USBH_PORT_ENUM_RETRY_MS = 100,
  USBH_PORT_ENUM_DRAIN_PASSES = 3,
  USBH_PORT_TASK_EVENT_BUDGET = 8,
  USBH_PORT_ENUM_EVENT_NONE = 0,
  USBH_PORT_ENUM_EVENT_FAILED,
  USBH_PORT_ENUM_EVENT_TIMEOUT,
  USBH_PORT_ENUM_EVENT_ATTACH_OVERFLOW,
  USBH_PORT_ENUM_EVENT_CONFIG_NOMEM,
  USBH_PORT_ENUM_EVENT_CONFIG_TOO_LARGE,
  USBH_PORT_ENUM_EVENT_CONFIG_INVALID
};

enum {
  USBH_PORT_ENUM_DELAY_NONE = 0,
  USBH_PORT_ENUM_DELAY_ROOT_RESET,
  USBH_PORT_ENUM_DELAY_DEBOUNCE,
  USBH_PORT_ENUM_DELAY_SET_ADDRESS,
  USBH_PORT_ENUM_DELAY_RETRY
};

static struct {
  uint32_t progress_ms;
  uint32_t drain_at_ms;
  uint32_t drain_event_start;
  uint8_t* retry_buffer;
  uint8_t* config_buffer;
  uintptr_t retry_user_data;
  tusb_control_request_t retry_request;
  uint16_t config_len;
  uint8_t daddr;
  uint8_t retry_daddr;
  uint8_t failed_count;
  uint8_t drain_passes;
  // Host-task diagnostics survive an immediately chained enumeration epoch.
  uint8_t report_event;
  bool attach_overflow_pending;
  uint8_t removed_hub_addr;
  uint8_t removed_hub_port;
  uint8_t removed_scope_rank;
  volatile bool aborting;
  uint8_t delay_state;
  bool removed;
  bool timed_out;
  bool restart_pending;
  bool foreign_fenced;
  bool drain_started;
  bool drain_queue_fencing;
  bool config_fetch_pending;
} _enum_port;

// Current TinyUSB uses a separate deferred-attach queue. Keep only the
// connection topology needed to rebuild an ATTACH event, avoiding both a
// blocking send back into the sole consumer's queue and another RTOS queue.
static struct {
  uint8_t rhport;
  uint8_t hub_addr;
  uint8_t hub_port;
} _enum_deferred_attach[TOTAL_DEVICES];
static uint8_t _enum_deferred_attach_count;

// Upstream TinyUSB: no queue-generation fence. A monotonic dequeue count lets
// the host prove that one complete FIFO generation passed even when deferred
// ATTACH events keep the queue permanently non-empty.
static uint32_t _usbh_port_event_count;

// all devices excluding zero-address
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration owner"
    TINYUSB_USBH_ENUM_STATE_UPSTREAM TINYUSB_USBH_ENUM_STATE_PORT)

set(TINYUSB_USBH_ENUM_HELPERS_UPSTREAM [=[
static bool enum_new_device(hcd_event_t* event);
static void process_removing_device(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port);
static bool usbh_edpt_control_open(uint8_t dev_addr, uint8_t max_packet_size);
static bool usbh_control_xfer_cb (uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
]=])
set(TINYUSB_USBH_ENUM_HELPERS_PORT [=[
static bool enum_new_device(hcd_event_t* event);
static void process_removing_device(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port);
static bool usbh_edpt_control_open(uint8_t dev_addr, uint8_t max_packet_size);
static bool usbh_control_xfer_cb (uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);

// Upstream TinyUSB: no equivalent; these callbacks are mandatory parts of the
// firmware lifecycle boundary. Keep declarations here and let a missing strong
// implementation fail at link time instead of silently selecting a weak stub.
extern bool tuh_reenumerate_begin_cb(uint8_t rhport, uint8_t hub_addr,
                                     uint8_t hub_port);
extern void tuh_port_enum_state_cb(uint8_t rhport, uint8_t hub_addr,
                                   uint8_t hub_port, bool active,
                                   bool success);
extern void tuh_port_control_idle_cb(void);
extern void tuh_port_enum_event_cb(uint8_t event);
extern void* tuh_port_enum_buffer_alloc_on_host(uint16_t length);
extern void tuh_port_enum_buffer_free_on_host(void* buffer);

// Upstream TinyUSB: no equivalent; forward declaration for exact pre-address
// hub-control ownership and the host-only terminal state below.
static void process_enumeration(tuh_xfer_t* xfer);
TU_ATTR_ALWAYS_INLINE static inline void _set_control_xfer_stage(uint8_t stage);
static void _control_xfer_complete(uint8_t daddr, xfer_result_t result);
static int usbh_port_enum_service_on_host(uint32_t* wait_ms);
static bool usbh_port_enum_config_has_interfaces_on_host(
    uint8_t const* config, uint16_t total_len, uint16_t offset,
    uint8_t count, uint8_t interface_len);
static bool usbh_port_enum_config_valid_on_host(uint8_t const* config,
                                                uint16_t total_len);
static void usbh_port_enum_config_release_on_host(void);

// Upstream TinyUSB: no enum-aware REMOVE ancestry fence. Rank the matching
// scope while the configured parent chain still exists; a larger rank is a
// broader ancestor and cannot later be narrowed by another queued REMOVE.
static uint8_t usbh_port_enum_remove_scope_rank_on_host(uint8_t rhport,
                                                        uint8_t hub_addr,
                                                        uint8_t hub_port) {
  if (!_dev0.enumerating || rhport != _dev0.rhport) return 0;
  if (hub_addr == 0) return (uint8_t) (2 * TOTAL_DEVICES + 1);

  uint8_t parent_addr = _dev0.hub_addr;
  uint8_t parent_port = _dev0.hub_port;
  for (uint8_t depth = 0; depth < TOTAL_DEVICES; depth++) {
    if (parent_addr == hub_addr &&
        (hub_port == 0 || parent_port == hub_port)) {
      return (uint8_t) (2 * depth + (hub_port == 0 ? 2 : 1));
    }
    if (parent_addr == 0) return 0;

    usbh_device_t const* parent = get_device(parent_addr);
    if (!parent || !parent->connected) return 0;
    parent_port = parent->hub_port;
    parent_addr = parent->hub_addr;
  }
  return 0;
}

static bool usbh_port_scope_contains_device_on_host(uint8_t hub_addr,
                                                     uint8_t hub_port,
                                                     uint8_t daddr) {
  usbh_device_t const* dev = get_device(daddr);
  if (!dev || !dev->connected || dev->rhport != _dev0.rhport) return false;
  if (hub_addr == 0) return true;

  uint8_t parent_addr = dev->hub_addr;
  uint8_t parent_port = dev->hub_port;
  for (uint8_t depth = 0; depth < TOTAL_DEVICES; depth++) {
    if (parent_addr == hub_addr &&
        (hub_port == 0 || parent_port == hub_port)) return true;
    if (parent_addr == 0) return false;

    usbh_device_t const* parent = get_device(parent_addr);
    if (!parent || !parent->connected) return false;
    parent_port = parent->hub_port;
    parent_addr = parent->hub_addr;
  }
  return false;
}

static bool usbh_port_defer_attach_on_host(hcd_event_t const* event) {
  for (uint8_t i = 0; i < _enum_deferred_attach_count; i++) {
    if (_enum_deferred_attach[i].rhport == event->rhport &&
        _enum_deferred_attach[i].hub_addr == event->connection.hub_addr &&
        _enum_deferred_attach[i].hub_port == event->connection.hub_port)
      return true;
  }

  if (_enum_deferred_attach_count >= TOTAL_DEVICES) return false;
  _enum_deferred_attach[_enum_deferred_attach_count].rhport = event->rhport;
  _enum_deferred_attach[_enum_deferred_attach_count].hub_addr =
      event->connection.hub_addr;
  _enum_deferred_attach[_enum_deferred_attach_count].hub_port =
      event->connection.hub_port;
  _enum_deferred_attach_count++;
  return true;
}

static bool usbh_port_take_deferred_attach_on_host(hcd_event_t* event) {
  if (_enum_deferred_attach_count == 0) return false;

  *event = (hcd_event_t) {
    .rhport = _enum_deferred_attach[0].rhport,
    .event_id = HCD_EVENT_DEVICE_ATTACH,
    .connection = {
      .hub_addr = _enum_deferred_attach[0].hub_addr,
      .hub_port = _enum_deferred_attach[0].hub_port
    }
  };

  _enum_deferred_attach_count--;
  for (uint8_t i = 0; i < _enum_deferred_attach_count; i++)
    _enum_deferred_attach[i] = _enum_deferred_attach[i + 1];
  return true;
}

static void usbh_port_retire_deferred_attach_on_host(uint8_t rhport,
                                                      uint8_t hub_addr,
                                                      uint8_t hub_port) {
  uint8_t write = 0;
  for (uint8_t read = 0; read < _enum_deferred_attach_count; read++) {
    bool retire = false;
    if (_enum_deferred_attach[read].rhport == rhport) {
      if (hub_addr == 0) {
        retire = true;
      } else if (_enum_deferred_attach[read].hub_addr == hub_addr &&
                 (hub_port == 0 ||
                  _enum_deferred_attach[read].hub_port == hub_port)) {
        retire = true;
      } else if (_enum_deferred_attach[read].hub_addr != 0) {
        retire = usbh_port_scope_contains_device_on_host(
            hub_addr, hub_port, _enum_deferred_attach[read].hub_addr);
      }
    }

    if (!retire) {
      if (write != read)
        _enum_deferred_attach[write] = _enum_deferred_attach[read];
      write++;
    }
  }
  _enum_deferred_attach_count = write;
}

static bool usbh_port_enum_noncontrol_event_retired_on_host(uint8_t daddr) {
  if (!_enum_port.aborting || daddr == 0) return false;
  if (_enum_port.removed) {
    return usbh_port_scope_contains_device_on_host(
        _enum_port.removed_hub_addr, _enum_port.removed_hub_port, daddr);
  }
  return _enum_port.daddr && daddr == _enum_port.daddr;
}

static bool usbh_port_enum_xfer_owned_on_host(uint8_t daddr,
                                               tuh_xfer_cb_t complete_cb) {
  if (!_enum_port.aborting) return false;
  if (daddr == 0 || (_enum_port.daddr && daddr == _enum_port.daddr)) return true;
  return !_enum_port.daddr && _dev0.hub_addr &&
         daddr == _dev0.hub_addr && complete_cb == process_enumeration;
}

static bool usbh_port_enum_control_owned_on_host(void) {
  if (_ctrl_xfer.stage == CONTROL_STAGE_IDLE) return false;
  if (_ctrl_xfer.daddr == 0 ||
      (_enum_port.daddr && _ctrl_xfer.daddr == _enum_port.daddr)) return true;
  return !_enum_port.daddr && _dev0.hub_addr &&
         _ctrl_xfer.daddr == _dev0.hub_addr &&
         _ctrl_xfer.complete_cb == process_enumeration;
}

static void usbh_port_enum_start_on_host(void) {
  _enum_port.progress_ms = tusb_time_millis_api();
  _enum_port.drain_at_ms = 0;
  _enum_port.drain_event_start = 0;
  _enum_port.retry_buffer = NULL;
  _enum_port.config_buffer = NULL;
  _enum_port.retry_user_data = 0;
  _enum_port.config_len = 0;
  _enum_port.daddr = 0;
  _enum_port.retry_daddr = 0;
  _enum_port.failed_count = 0;
  _enum_port.drain_passes = 0;
  _enum_port.removed_hub_addr = 0;
  _enum_port.removed_hub_port = 0;
  _enum_port.removed_scope_rank = 0;
  _enum_port.aborting = false;
  _enum_port.delay_state = USBH_PORT_ENUM_DELAY_NONE;
  _enum_port.removed = false;
  _enum_port.timed_out = false;
  _enum_port.restart_pending = false;
  _enum_port.foreign_fenced = false;
  _enum_port.drain_started = false;
  _enum_port.drain_queue_fencing = false;
  _enum_port.config_fetch_pending = false;
}

static void usbh_port_enum_control_progress_on_host(uint8_t daddr) {
  if (_dev0.enumerating && !_enum_port.aborting &&
      _ctrl_xfer.daddr == daddr && usbh_port_enum_control_owned_on_host()) {
    _enum_port.progress_ms = tusb_time_millis_api();
    tuh_port_enum_state_cb(_dev0.rhport, _dev0.hub_addr, _dev0.hub_port,
                           true, false);
  }
}
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration helpers"
    TINYUSB_USBH_ENUM_HELPERS_UPSTREAM TINYUSB_USBH_ENUM_HELPERS_PORT)

set(TINYUSB_USBH_ENUM_ABORT_GATE_UPSTREAM [=[
  // Check if device is still connected (enumerating for dev0)
  const uint8_t daddr = xfer->daddr;
]=])
set(TINYUSB_USBH_ENUM_ABORT_GATE_PORT [=[
  // Upstream TinyUSB:
  // // Check if device is still connected (enumerating for dev0)
  // const uint8_t daddr = xfer->daddr;
  const uint8_t daddr = xfer->daddr;
  // A timed-out callback may try to chain another enum/class request while its
  // preceding physical stage drains. Keep the same logical EP0 owner occupied.
  if (usbh_port_enum_xfer_owned_on_host(daddr, xfer->complete_cb)) return false;
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration abort gate"
    TINYUSB_USBH_ENUM_ABORT_GATE_UPSTREAM TINYUSB_USBH_ENUM_ABORT_GATE_PORT)

set(TINYUSB_USBH_SETUP_SUBMIT_UPSTREAM [=[
  if (xfer->complete_cb) {
    TU_ASSERT( hcd_setup_send(rhport, daddr, (uint8_t const*) &_usbh_epbuf.request) );
  }else {
]=])
set(TINYUSB_USBH_SETUP_SUBMIT_PORT [=[
  if (xfer->complete_cb) {
    // TU_ASSERT( hcd_setup_send(rhport, daddr, (uint8_t const*) &_usbh_epbuf.request) );
    // Current TinyUSB restores its logical owner when SETUP submission fails.
    if (!hcd_setup_send(rhport, daddr, (uint8_t const*) &_usbh_epbuf.request)) {
      _set_control_xfer_stage(CONTROL_STAGE_IDLE);
      return false;
    }
  }else {
]=])
ergotype_tinyusb_usbh_replace_unique("async SETUP submission"
    TINYUSB_USBH_SETUP_SUBMIT_UPSTREAM TINYUSB_USBH_SETUP_SUBMIT_PORT)

set(TINYUSB_USBH_CONTROL_PROGRESS_UPSTREAM [=[
  tusb_control_request_t const * request = &_usbh_epbuf.request;

  if (XFER_RESULT_SUCCESS != result) {
]=])
set(TINYUSB_USBH_CONTROL_PROGRESS_PORT [=[
  tusb_control_request_t const * request = &_usbh_epbuf.request;

  // Upstream TinyUSB: no equivalent; any exact HCD event advances the finite
  // enumeration phase unless terminal physical draining has already begun.
  usbh_port_enum_control_progress_on_host(daddr);

  if (XFER_RESULT_SUCCESS != result) {
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration progress"
    TINYUSB_USBH_CONTROL_PROGRESS_UPSTREAM TINYUSB_USBH_CONTROL_PROGRESS_PORT)

set(TINYUSB_USBH_DATA_SUBMIT_UPSTREAM [=[
          TU_ASSERT( hcd_edpt_xfer(rhport, daddr, tu_edpt_addr(0, request->bmRequestType_bit.direction), _ctrl_xfer.buffer, request->wLength) );
          return true;
]=])
set(TINYUSB_USBH_DATA_SUBMIT_PORT [=[
          // TU_ASSERT( hcd_edpt_xfer(rhport, daddr, tu_edpt_addr(0, request->bmRequestType_bit.direction), _ctrl_xfer.buffer, request->wLength) );
          // A rejected DATA stage is a terminal callback result, not a busy owner.
          if (!hcd_edpt_xfer(rhport, daddr,
                             tu_edpt_addr(0, request->bmRequestType_bit.direction),
                             _ctrl_xfer.buffer, request->wLength)) {
            _control_xfer_complete(daddr, XFER_RESULT_FAILED);
            return false;
          }
          return true;
]=])
ergotype_tinyusb_usbh_replace_unique("control DATA submission"
    TINYUSB_USBH_DATA_SUBMIT_UPSTREAM TINYUSB_USBH_DATA_SUBMIT_PORT)

set(TINYUSB_USBH_ACK_SUBMIT_UPSTREAM [=[
        TU_ASSERT( hcd_edpt_xfer(rhport, daddr, tu_edpt_addr(0, 1 - request->bmRequestType_bit.direction), NULL, 0) );
        break;
]=])
set(TINYUSB_USBH_ACK_SUBMIT_PORT [=[
        // TU_ASSERT( hcd_edpt_xfer(rhport, daddr, tu_edpt_addr(0, 1 - request->bmRequestType_bit.direction), NULL, 0) );
        // A rejected ACK stage must release the exact logical callback owner.
        if (!hcd_edpt_xfer(rhport, daddr,
                           tu_edpt_addr(0, 1 - request->bmRequestType_bit.direction),
                           NULL, 0)) {
          _control_xfer_complete(daddr, XFER_RESULT_FAILED);
          return false;
        }
        break;
]=])
ergotype_tinyusb_usbh_replace_unique("control ACK submission"
    TINYUSB_USBH_ACK_SUBMIT_UPSTREAM TINYUSB_USBH_ACK_SUBMIT_PORT)

set(TINYUSB_USBH_ATTACH_UPSTREAM [=[
          if (event.rhport == _dev0.rhport && event.connection.hub_addr == _dev0.hub_addr &&
              event.connection.hub_port == _dev0.hub_port) {
            // abort/cancel current enumeration and start new one
            TU_LOG1("[%u:] USBH Device Attach (duplicated)\r\n", event.rhport);
            tuh_edpt_abort_xfer(0, 0);
            enum_new_device(&event);
          } else {
            TU_LOG_USBH("[%u:] USBH Defer Attach until current enumeration complete\r\n", event.rhport);

            bool is_empty = osal_queue_empty(_usbh_q);
            queue_event(&event, in_isr);

            if (is_empty) {
              // Exit if this is the only event in the queue, otherwise we may loop forever
              return;
            }
          }
]=])
set(TINYUSB_USBH_ATTACH_PORT [=[
          if (event.rhport == _dev0.rhport && event.connection.hub_addr == _dev0.hub_addr &&
              event.connection.hub_port == _dev0.hub_port) {
            // Upstream TinyUSB:
            // // abort/cancel current enumeration and start new one
            // TU_LOG1("[%u:] USBH Device Attach (duplicated)\r\n", event.rhport);
            // tuh_edpt_abort_xfer(0, 0);
            // enum_new_device(&event);
            // A duplicate can be the only durable signal after a lost REMOVE.
            // Retire the old exact owner, then start one fresh host-owned epoch;
            // additional duplicates coalesce only into that restart intent.
            TU_LOG1("[%u:] USBH Device Attach (deferred restart)\r\n",
                    event.rhport);
            _enum_port.restart_pending = true;
            if (!_enum_port.aborting) {
              _enum_port.aborting = true;
              _enum_port.drain_passes = 0;
              _enum_port.drain_at_ms = tusb_time_millis_api();
              _enum_port.drain_event_start = 0;
              _enum_port.drain_started = false;
              _enum_port.drain_queue_fencing = false;
            }
            return;
          } else {
            TU_LOG_USBH("[%u:] USBH Defer Attach until current enumeration complete\r\n", event.rhport);

            // Upstream TinyUSB:
            // bool is_empty = osal_queue_empty(_usbh_q);
            // queue_event(&event, in_isr);
            //
            // if (is_empty) {
            //   // Exit if this is the only event in the queue, otherwise we may loop forever
            //   return;
            // }
            // Sending to the sole consumer's own full queue can deadlock. Keep
            // bounded topology for the first host service after terminal unwind.
            if (!usbh_port_defer_attach_on_host(&event))
              _enum_port.attach_overflow_pending = true;
          }
]=])
ergotype_tinyusb_usbh_replace_unique("attach ownership"
    TINYUSB_USBH_ATTACH_UPSTREAM TINYUSB_USBH_ATTACH_PORT)

set(TINYUSB_USBH_ATTACH_START_UPSTREAM [=[
          _dev0.enumerating = 1;
          enum_new_device(&event);
]=])
set(TINYUSB_USBH_ATTACH_START_PORT [=[
          _dev0.enumerating = 1;
          // Upstream TinyUSB: no equivalent exact enumeration owner.
          usbh_port_enum_start_on_host();
          enum_new_device(&event);
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration begin"
    TINYUSB_USBH_ATTACH_START_UPSTREAM TINYUSB_USBH_ATTACH_START_PORT)

set(TINYUSB_USBH_REMOVE_HOST_UPSTREAM [=[
      case HCD_EVENT_DEVICE_REMOVE:
        TU_LOG_USBH("[%u:%u:%u] USBH DEVICE REMOVED\r\n", event.rhport, event.connection.hub_addr, event.connection.hub_port);
        process_removing_device(event.rhport, event.connection.hub_addr, event.connection.hub_port);

        #if CFG_TUH_HUB
        // TODO remove
        if (event.connection.hub_addr != 0 && event.connection.hub_port != 0) {
          // done with hub, waiting for next data on status pipe
          (void) hub_edpt_status_xfer(event.connection.hub_addr);
        }
        #endif
        break;
]=])
set(TINYUSB_USBH_REMOVE_HOST_PORT [=[
      // Upstream TinyUSB:
      // case HCD_EVENT_DEVICE_REMOVE:
      //   TU_LOG_USBH("[%u:%u:%u] USBH DEVICE REMOVED\r\n", event.rhport, event.connection.hub_addr, event.connection.hub_port);
      //   process_removing_device(event.rhport, event.connection.hub_addr, event.connection.hub_port);
      //
      //   #if CFG_TUH_HUB
      //   // TODO remove
      //   if (event.connection.hub_addr != 0 && event.connection.hub_port != 0) {
      //     // done with hub, waiting for next data on status pipe
      //     (void) hub_edpt_status_xfer(event.connection.hub_addr);
      //   }
      //   #endif
      //   break;
      case HCD_EVENT_DEVICE_REMOVE: {
        TU_LOG_USBH("[%u:%u:%u] USBH DEVICE REMOVED\r\n", event.rhport,
                    event.connection.hub_addr, event.connection.hub_port);
        uint8_t const enum_remove_rank =
            usbh_port_enum_remove_scope_rank_on_host(
                event.rhport, event.connection.hub_addr,
                event.connection.hub_port);
        // Current TinyUSB retires delayed connection events with the removed
        // topology. Do this before process_removing_device() clears ancestry.
        usbh_port_retire_deferred_attach_on_host(
            event.rhport, event.connection.hub_addr,
            event.connection.hub_port);

        if (enum_remove_rank) {
          // Keep the partial device and exact EP0 owner intact until the host
          // owner service observes completion or finishes the bounded drain.
          if (!_enum_port.aborting) {
            _enum_port.aborting = true;
            _enum_port.drain_passes = 0;
            _enum_port.drain_at_ms = tusb_time_millis_api();
            _enum_port.drain_event_start = 0;
            _enum_port.drain_started = false;
            _enum_port.drain_queue_fencing = false;
          }
          _enum_port.removed = true;
          if (enum_remove_rank > _enum_port.removed_scope_rank) {
            _enum_port.removed_scope_rank = enum_remove_rank;
            _enum_port.removed_hub_addr = event.connection.hub_addr;
            _enum_port.removed_hub_port = event.connection.hub_port;
          }
          // No later class callback may run on the removed partial topology.
          // Return to the host loop before consuming the next queued event.
          return;
        } else {
          process_removing_device(event.rhport, event.connection.hub_addr,
                                  event.connection.hub_port);

          #if CFG_TUH_HUB
          if (event.connection.hub_addr != 0 && event.connection.hub_port != 0) {
            (void) hub_edpt_status_xfer(event.connection.hub_addr);
          }
          #endif
        }
        break;
      }
]=])
ergotype_tinyusb_usbh_replace_unique("host remove terminal fence"
    TINYUSB_USBH_REMOVE_HOST_UPSTREAM TINYUSB_USBH_REMOVE_HOST_PORT)

set(TINYUSB_USBH_RETIRED_EVENT_UPSTREAM [=[
        uint8_t const ep_addr = event.xfer_complete.ep_addr;
        uint8_t const epnum = tu_edpt_number(ep_addr);
        uint8_t const ep_dir = (uint8_t) tu_edpt_dir(ep_addr);

        TU_LOG_USBH("on EP %02X with %u bytes: %s\r\n", ep_addr, (unsigned int) event.xfer_complete.len, tu_str_xfer_result[event.xfer_complete.result]);
]=])
set(TINYUSB_USBH_RETIRED_EVENT_PORT [=[
        uint8_t const ep_addr = event.xfer_complete.ep_addr;
        uint8_t const epnum = tu_edpt_number(ep_addr);
        uint8_t const ep_dir = (uint8_t) tu_edpt_dir(ep_addr);

        // Upstream TinyUSB: no terminal enum fence. EP0 must still drain, but
        // interrupt completions from a removed partial subtree cannot rearm it.
        if (epnum &&
            usbh_port_enum_noncontrol_event_retired_on_host(event.dev_addr))
          break;

        TU_LOG_USBH("on EP %02X with %u bytes: %s\r\n", ep_addr, (unsigned int) event.xfer_complete.len, tu_str_xfer_result[event.xfer_complete.result]);
]=])
ergotype_tinyusb_usbh_replace_unique("retired enumeration event fence"
    TINYUSB_USBH_RETIRED_EVENT_UPSTREAM TINYUSB_USBH_RETIRED_EVENT_PORT)

set(TINYUSB_USBH_REMOVE_ISR_UPSTREAM [=[
      // Check if dev0 is removed
      if ((event->rhport == _dev0.rhport) && (event->connection.hub_addr == _dev0.hub_addr) &&
          (event->connection.hub_port == _dev0.hub_port)) {
        _dev0.enumerating = 0;
      }
]=])
set(TINYUSB_USBH_REMOVE_ISR_PORT [=[
      // Upstream TinyUSB:
      // // Check if dev0 is removed
      // if ((event->rhport == _dev0.rhport) && (event->connection.hub_addr == _dev0.hub_addr) &&
      //     (event->connection.hub_port == _dev0.hub_port)) {
      //   _dev0.enumerating = 0;
      // }
      // Firmware keeps all enumeration-owner mutation in the TinyUSB task. The
      // queued REMOVE above reaches the host fence before another ATTACH can run.
]=])
ergotype_tinyusb_usbh_replace_unique("ISR remove ownership"
    TINYUSB_USBH_REMOVE_ISR_UPSTREAM TINYUSB_USBH_REMOVE_ISR_PORT)

set(TINYUSB_USBH_TASK_BUDGET_START_UPSTREAM [=[
void tuh_task_ext(uint32_t timeout_ms, bool in_isr) {
  (void) in_isr; // not implemented yet

  // Skip if stack is not initialized
  if (!tuh_inited()) return;
]=])
set(TINYUSB_USBH_TASK_BUDGET_START_PORT [=[
void tuh_task_ext(uint32_t timeout_ms, bool in_isr) {
  (void) in_isr; // not implemented yet

  // Upstream TinyUSB: no finite event budget; timeout applies to every receive.
  // Blocking control already calls tuh_task() repeatedly until its result is
  // terminal, so every invocation can return to its caller after this budget.
  uint8_t port_event_budget = USBH_PORT_TASK_EVENT_BUDGET;

  // Skip if stack is not initialized
  if (!tuh_inited()) return;

  // Upstream TinyUSB: the pinned task has no internal delayed continuation.
  // Current TinyUSB shortens its private queue wait to the nearest call-after
  // deadline. Keep that mechanism internal so firmware still calls tuh_task().
  uint32_t enum_wait_ms;
  int const enum_event = usbh_port_enum_service_on_host(&enum_wait_ms);
  if (enum_event != USBH_PORT_ENUM_EVENT_NONE) {
    tuh_port_enum_event_cb((uint8_t) enum_event);
    return;
  }
  if (enum_wait_ms < timeout_ms) timeout_ms = enum_wait_ms;
]=])
ergotype_tinyusb_usbh_replace_unique("finite host event budget"
    TINYUSB_USBH_TASK_BUDGET_START_UPSTREAM
    TINYUSB_USBH_TASK_BUDGET_START_PORT)

set(TINYUSB_USBH_TASK_DEQUEUE_UPSTREAM [=[
    hcd_event_t event;
    if (!osal_queue_receive(_usbh_q, &event, timeout_ms)) return;

    switch (event.event_id) {
]=])
set(TINYUSB_USBH_TASK_DEQUEUE_PORT [=[
    hcd_event_t event;
    if (!osal_queue_receive(_usbh_q, &event, timeout_ms)) return;
    // Upstream TinyUSB: no dequeue-generation fence. Count before dispatch so
    // an event branch which returns still advances a waiting physical drain.
    _usbh_port_event_count++;

    switch (event.event_id) {
]=])
ergotype_tinyusb_usbh_replace_unique("host dequeue generation"
    TINYUSB_USBH_TASK_DEQUEUE_UPSTREAM TINYUSB_USBH_TASK_DEQUEUE_PORT)

set(TINYUSB_USBH_TASK_BUDGET_END_UPSTREAM [=[
#if CFG_TUSB_OS != OPT_OS_NONE && CFG_TUSB_OS != OPT_OS_PICO
    // return if there is no more events, for application to run other background
    if (osal_queue_empty(_usbh_q)) return;
#endif
  }
}
]=])
set(TINYUSB_USBH_TASK_BUDGET_END_PORT [=[
    // Upstream TinyUSB:
    // #if CFG_TUSB_OS != OPT_OS_NONE && CFG_TUSB_OS != OPT_OS_PICO
    //   // return if there is no more events, for application to run other background
    //   if (osal_queue_empty(_usbh_q)) return;
    // #endif
#if CFG_TUSB_OS != OPT_OS_NONE && CFG_TUSB_OS != OPT_OS_PICO
    if (osal_queue_empty(_usbh_q)) return;
#endif
    if (--port_event_budget == 0) return;
  }
}
]=])
ergotype_tinyusb_usbh_replace_unique("finite host event return"
    TINYUSB_USBH_TASK_BUDGET_END_UPSTREAM
    TINYUSB_USBH_TASK_BUDGET_END_PORT)

set(TINYUSB_USBH_ENUM_COMPLETE_DECL_UPSTREAM [=[
static bool enum_request_set_addr(void);
static bool _parse_configuration_descriptor (uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg);
static void enum_full_complete(void);
]=])
set(TINYUSB_USBH_ENUM_COMPLETE_DECL_PORT [=[
static bool enum_request_set_addr(void);
static bool _parse_configuration_descriptor (uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg);
// Upstream TinyUSB:
// static void enum_full_complete(void);
// The firmware terminal fence can defer failure until its exact EP0 owner has
// physically drained, and rejects a late success after retirement has begun.
static bool enum_full_complete(bool success);
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration terminal declaration"
    TINYUSB_USBH_ENUM_COMPLETE_DECL_UPSTREAM
    TINYUSB_USBH_ENUM_COMPLETE_DECL_PORT)

set(TINYUSB_USBH_ENUM_RETRY_UPSTREAM [=[
  static uint8_t failed_count = 0;

  if (XFER_RESULT_SUCCESS != xfer->result) {
    // retry if not reaching max attempt
    bool retry = _dev0.enumerating && (failed_count < ATTEMPT_COUNT_MAX);
    if ( retry ) {
      failed_count++;
      tusb_time_delay_ms_api(ATTEMPT_DELAY_MS); // delay a bit
      TU_LOG1("Enumeration attempt %u\r\n", failed_count);
      retry = tuh_control_xfer(xfer);
    }

    if (!retry) {
      enum_full_complete();
    }

    return;
  }
  failed_count = 0;
]=])
set(TINYUSB_USBH_ENUM_RETRY_PORT [=[
  // Upstream TinyUSB:
  // static uint8_t failed_count = 0;
  //
  // if (XFER_RESULT_SUCCESS != xfer->result) {
  //   // retry if not reaching max attempt
  //   bool retry = _dev0.enumerating && (failed_count < ATTEMPT_COUNT_MAX);
  //   if ( retry ) {
  //     failed_count++;
  //     tusb_time_delay_ms_api(ATTEMPT_DELAY_MS); // delay a bit
  //     TU_LOG1("Enumeration attempt %u\r\n", failed_count);
  //     retry = tuh_control_xfer(xfer);
  //   }
  //
  //   if (!retry) {
  //     enum_full_complete();
  //   }
  //
  //   return;
  // }
  // failed_count = 0;
  // A REMOVE or timeout may race the last physical completion. Completion has
  // already released the logical owner, so terminalize without chaining it.
  if (!_dev0.enumerating) return;
  if (xfer->daddr != 0 &&
      !(_enum_port.daddr && xfer->daddr == _enum_port.daddr) &&
      !(!_enum_port.daddr && _dev0.hub_addr &&
        xfer->daddr == _dev0.hub_addr)) return;
  if (_enum_port.aborting) {
    (void) enum_full_complete(false);
    return;
  }

  if (XFER_RESULT_SUCCESS != xfer->result) {
    bool retry = _dev0.enumerating &&
                 (_enum_port.failed_count < ATTEMPT_COUNT_MAX);
    if (retry) {
      uint32_t const retry_now = tusb_time_millis_api();

      _enum_port.failed_count++;
      // The callback's tuh_xfer_t and setup packet are stack-local in
      // _control_xfer_complete(). Snapshot every value needed by a later
      // submission; its data buffer remains owned by this enumeration epoch.
      _enum_port.retry_request = *xfer->setup;
      _enum_port.retry_buffer = xfer->buffer;
      _enum_port.retry_user_data = xfer->user_data;
      _enum_port.retry_daddr = xfer->daddr;
      _enum_port.progress_ms = retry_now;
      _enum_port.delay_state = USBH_PORT_ENUM_DELAY_RETRY;
    }

    if (!retry) (void) enum_full_complete(false);
    return;
  }
  _enum_port.failed_count = 0;
  _enum_port.retry_buffer = NULL;
  _enum_port.retry_user_data = 0;
  _enum_port.retry_daddr = 0;
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration retry epoch"
    TINYUSB_USBH_ENUM_RETRY_UPSTREAM TINYUSB_USBH_ENUM_RETRY_PORT)

set(TINYUSB_USBH_ENUM_HUB_RESET_1_UPSTREAM [=[
      if (!port_status.status.connection) {
        // device unplugged while delaying, nothing else to do
        enum_full_complete();
        return;
      }

      _dev0.speed = (port_status.status.high_speed) ? TUSB_SPEED_HIGH :
                    (port_status.status.low_speed) ? TUSB_SPEED_LOW : TUSB_SPEED_FULL;

      // Acknowledge Port Reset Change
      if (port_status.change.reset) {
        hub_port_clear_reset_change(_dev0.hub_addr, _dev0.hub_port,
                                    process_enumeration, ENUM_ADDR0_DEVICE_DESC);
      }
      break;
]=])
set(TINYUSB_USBH_ENUM_HUB_RESET_1_PORT [=[
      if (!port_status.status.connection) {
        // Upstream TinyUSB:
        // // device unplugged while delaying, nothing else to do
        // enum_full_complete();
        _enum_port.removed = true;
        _enum_port.removed_hub_addr = _dev0.hub_addr;
        _enum_port.removed_hub_port = _dev0.hub_port;
        _enum_port.removed_scope_rank = 1;
        (void) enum_full_complete(false);
        return;
      }

      _dev0.speed = (port_status.status.high_speed) ? TUSB_SPEED_HIGH :
                    (port_status.status.low_speed) ? TUSB_SPEED_LOW : TUSB_SPEED_FULL;

      // Upstream TinyUSB:
      // // Acknowledge Port Reset Change
      // if (port_status.change.reset) {
      //   hub_port_clear_reset_change(_dev0.hub_addr, _dev0.hub_port,
      //                               process_enumeration, ENUM_ADDR0_DEVICE_DESC);
      // }
      // Missing RESET_CHANGE or synchronous hub submission rejection otherwise
      // leaves the one enumeration epoch live without any future callback.
      if (!port_status.change.reset ||
          !hub_port_clear_reset_change(_dev0.hub_addr, _dev0.hub_port,
                                       process_enumeration,
                                       ENUM_ADDR0_DEVICE_DESC)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("first hub reset completion"
    TINYUSB_USBH_ENUM_HUB_RESET_1_UPSTREAM
    TINYUSB_USBH_ENUM_HUB_RESET_1_PORT)

set(TINYUSB_USBH_ENUM_ADDR0_UPSTREAM [=[
    case ENUM_ADDR0_DEVICE_DESC: {
      // TODO probably doesn't need to open/close each enumeration
      uint8_t const addr0 = 0;
      TU_ASSERT(usbh_edpt_control_open(addr0, 8),);

      // Get first 8 bytes of device descriptor for Control Endpoint size
      TU_LOG_USBH("Get 8 byte of Device Descriptor\r\n");
      TU_ASSERT(tuh_descriptor_get_device(addr0, _usbh_epbuf.ctrl, 8,
                                          process_enumeration, ENUM_SET_ADDR),);
      break;
    }
]=])
set(TINYUSB_USBH_ENUM_ADDR0_PORT [=[
    // Upstream TinyUSB:
    // case ENUM_ADDR0_DEVICE_DESC: {
    //   // TODO probably doesn't need to open/close each enumeration
    //   uint8_t const addr0 = 0;
    //   TU_ASSERT(usbh_edpt_control_open(addr0, 8),);
    //
    //   // Get first 8 bytes of device descriptor for Control Endpoint size
    //   TU_LOG_USBH("Get 8 byte of Device Descriptor\r\n");
    //   TU_ASSERT(tuh_descriptor_get_device(addr0, _usbh_epbuf.ctrl, 8,
    //                                       process_enumeration, ENUM_SET_ADDR),);
    //   break;
    // }
    case ENUM_ADDR0_DEVICE_DESC: {
      uint8_t const addr0 = 0;
      if (!usbh_edpt_control_open(addr0, 8)) {
        (void) enum_full_complete(false);
        return;
      }

      TU_LOG_USBH("Get 8 byte of Device Descriptor\r\n");
      if (!tuh_descriptor_get_device(addr0, _usbh_epbuf.ctrl, 8,
                                     process_enumeration, ENUM_SET_ADDR)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
    }
]=])
ergotype_tinyusb_usbh_replace_unique("address-zero descriptor submission"
    TINYUSB_USBH_ENUM_ADDR0_UPSTREAM TINYUSB_USBH_ENUM_ADDR0_PORT)

set(TINYUSB_USBH_ENUM_SET_ADDR_UPSTREAM [=[
    case ENUM_SET_ADDR:
      enum_request_set_addr();
      break;
]=])
set(TINYUSB_USBH_ENUM_SET_ADDR_PORT [=[
    case ENUM_SET_ADDR:
      // Upstream TinyUSB:
      // enum_request_set_addr();
      if (!enum_request_set_addr()) {
        (void) enum_full_complete(false);
        return;
      }
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("set-address submission"
    TINYUSB_USBH_ENUM_SET_ADDR_UPSTREAM TINYUSB_USBH_ENUM_SET_ADDR_PORT)

set(TINYUSB_USBH_ENUM_DEVICE_DESC_UPSTREAM [=[
    case ENUM_GET_DEVICE_DESC: {
      // Allow 2ms for address recovery time, Ref USB Spec 9.2.6.3
      tusb_time_delay_ms_api(2);

      const uint8_t new_addr = (uint8_t) tu_le16toh(xfer->setup->wValue);

      usbh_device_t* new_dev = get_device(new_addr);
      TU_ASSERT(new_dev,);
      new_dev->addressed = 1;

      // Close device 0
      hcd_device_close(_dev0.rhport, 0);

      // open control pipe for new address
      TU_ASSERT(usbh_edpt_control_open(new_addr, new_dev->ep0_size),);

      // Get full device descriptor
      TU_LOG_USBH("Get Device Descriptor\r\n");
      TU_ASSERT(tuh_descriptor_get_device(new_addr, _usbh_epbuf.ctrl, sizeof(tusb_desc_device_t),
                                          process_enumeration, ENUM_GET_9BYTE_CONFIG_DESC),);
      break;
    }
]=])
set(TINYUSB_USBH_ENUM_DEVICE_DESC_PORT [=[
    // Upstream TinyUSB:
    // case ENUM_GET_DEVICE_DESC: {
    //   // Allow 2ms for address recovery time, Ref USB Spec 9.2.6.3
    //   tusb_time_delay_ms_api(2);
    //
    //   const uint8_t new_addr = (uint8_t) tu_le16toh(xfer->setup->wValue);
    //
    //   usbh_device_t* new_dev = get_device(new_addr);
    //   TU_ASSERT(new_dev,);
    //   new_dev->addressed = 1;
    //
    //   // Close device 0
    //   hcd_device_close(_dev0.rhport, 0);
    //
    //   // open control pipe for new address
    //   TU_ASSERT(usbh_edpt_control_open(new_addr, new_dev->ep0_size),);
    //
    //   // Get full device descriptor
    //   TU_LOG_USBH("Get Device Descriptor\r\n");
    //   TU_ASSERT(tuh_descriptor_get_device(new_addr, _usbh_epbuf.ctrl, sizeof(tusb_desc_device_t),
    //                                       process_enumeration, ENUM_GET_9BYTE_CONFIG_DESC),);
    //   break;
    // }
    case ENUM_GET_DEVICE_DESC: {
      // Current TinyUSB also resumes SET_ADDRESS recovery asynchronously. The
      // callback's xfer/setup storage is temporary; the exact provisional
      // address is already retained by the firmware enumeration owner.
      uint8_t const new_addr =
          (uint8_t) tu_le16toh(xfer->setup->wValue);
      if (!get_device(new_addr) || _enum_port.daddr != new_addr) {
        (void) enum_full_complete(false);
        return;
      }
      _enum_port.progress_ms = tusb_time_millis_api();
      _enum_port.delay_state = USBH_PORT_ENUM_DELAY_SET_ADDRESS;
      break;
    }
]=])
ergotype_tinyusb_usbh_replace_unique("addressed device descriptor"
    TINYUSB_USBH_ENUM_DEVICE_DESC_UPSTREAM
    TINYUSB_USBH_ENUM_DEVICE_DESC_PORT)

set(TINYUSB_USBH_ENUM_CONFIG_9_SUBMIT_UPSTREAM [=[
      TU_ASSERT(tuh_descriptor_get_configuration(daddr, config_idx, _usbh_epbuf.ctrl, 9,
                                                 process_enumeration, ENUM_GET_FULL_CONFIG_DESC),);
      break;
]=])
set(TINYUSB_USBH_ENUM_CONFIG_9_SUBMIT_PORT [=[
      // Upstream TinyUSB:
      // TU_ASSERT(tuh_descriptor_get_configuration(daddr, config_idx, _usbh_epbuf.ctrl, 9,
      //                                             process_enumeration, ENUM_GET_FULL_CONFIG_DESC),);
      if (!tuh_descriptor_get_configuration(daddr, config_idx, _usbh_epbuf.ctrl,
                                            9, process_enumeration,
                                            ENUM_GET_FULL_CONFIG_DESC)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("short configuration submission"
    TINYUSB_USBH_ENUM_CONFIG_9_SUBMIT_UPSTREAM
    TINYUSB_USBH_ENUM_CONFIG_9_SUBMIT_PORT)

set(TINYUSB_USBH_ENUM_CONFIG_FULL_UPSTREAM [=[
      // TODO not enough buffer to hold configuration descriptor
      TU_ASSERT(total_len <= CFG_TUH_ENUMERATION_BUFSIZE,);

      // Get full configuration descriptor
      uint8_t const config_idx = CONFIG_NUM - 1;
      TU_LOG_USBH("Get Configuration[0] Descriptor\r\n");
      TU_ASSERT(tuh_descriptor_get_configuration(daddr, config_idx, _usbh_epbuf.ctrl, total_len,
                                                 process_enumeration, ENUM_SET_CONFIG),);
      break;
]=])
set(TINYUSB_USBH_ENUM_CONFIG_FULL_PORT [=[
      // Upstream TinyUSB:
      // // TODO not enough buffer to hold configuration descriptor
      // TU_ASSERT(total_len <= CFG_TUH_ENUMERATION_BUFSIZE,);
      // Linux first validates the short header, then allocates wTotalLength.
      // Keep TinyUSB's permanent scratch for ordinary descriptors and defer
      // only the larger allocation until this callback has unwound.
      if (xfer->actual_len < sizeof(tusb_desc_configuration_t) ||
          desc_config[0] != sizeof(tusb_desc_configuration_t) ||
          desc_config[1] != TUSB_DESC_CONFIGURATION ||
          total_len < sizeof(tusb_desc_configuration_t)) {
        _enum_port.report_event = USBH_PORT_ENUM_EVENT_CONFIG_INVALID;
        (void) enum_full_complete(false);
        return;
      }
      if (total_len > ERGOTYPE_TUH_ENUMERATION_MAX_BUFSIZE) {
        _enum_port.report_event = USBH_PORT_ENUM_EVENT_CONFIG_TOO_LARGE;
        (void) enum_full_complete(false);
        return;
      }

      _enum_port.config_len = total_len;

      // Get full configuration descriptor
      uint8_t const config_idx = CONFIG_NUM - 1;
      TU_LOG_USBH("Get Configuration[0] Descriptor\r\n");
      // Upstream TinyUSB:
      // TU_ASSERT(tuh_descriptor_get_configuration(daddr, config_idx, _usbh_epbuf.ctrl, total_len,
      //                                             process_enumeration, ENUM_SET_CONFIG),);
      if (total_len > CFG_TUH_ENUMERATION_BUFSIZE) {
        _enum_port.progress_ms = tusb_time_millis_api();
        _enum_port.config_fetch_pending = true;
      } else if (!tuh_descriptor_get_configuration(
                     daddr, config_idx, _usbh_epbuf.ctrl, total_len,
                     process_enumeration, ENUM_SET_CONFIG)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("full configuration submission"
    TINYUSB_USBH_ENUM_CONFIG_FULL_UPSTREAM
    TINYUSB_USBH_ENUM_CONFIG_FULL_PORT)

set(TINYUSB_USBH_ENUM_SET_CONFIG_UPSTREAM [=[
    case ENUM_SET_CONFIG:
      TU_ASSERT(tuh_configuration_set(daddr, CONFIG_NUM, process_enumeration, ENUM_CONFIG_DRIVER),);
      break;
]=])
set(TINYUSB_USBH_ENUM_SET_CONFIG_PORT [=[
    case ENUM_SET_CONFIG: {
      uint8_t const* desc_config = _enum_port.config_buffer ?
          _enum_port.config_buffer : _usbh_epbuf.ctrl;

      // Upstream TinyUSB trusts a successful full GET. The firmware validates
      // the retained buffer before its Linux-shaped parser can walk it.
      if (!_enum_port.config_len ||
          xfer->actual_len != _enum_port.config_len ||
          !usbh_port_enum_config_valid_on_host(
              desc_config, _enum_port.config_len)) {
        _enum_port.report_event = USBH_PORT_ENUM_EVENT_CONFIG_INVALID;
        (void) enum_full_complete(false);
        return;
      }

      // Upstream TinyUSB:
      // TU_ASSERT(tuh_configuration_set(daddr, CONFIG_NUM, process_enumeration, ENUM_CONFIG_DRIVER),);
      if (!tuh_configuration_set(daddr, CONFIG_NUM, process_enumeration,
                                 ENUM_CONFIG_DRIVER)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
    }
]=])
ergotype_tinyusb_usbh_replace_unique("set-configuration submission"
    TINYUSB_USBH_ENUM_SET_CONFIG_UPSTREAM
    TINYUSB_USBH_ENUM_SET_CONFIG_PORT)

set(TINYUSB_USBH_ENUM_CONFIG_DRIVER_UPSTREAM [=[
      usbh_device_t* dev = get_device(daddr);
      TU_ASSERT(dev,);

      dev->configured = 1;

      // Parse configuration & set up drivers
      // driver_open() must not make any usb transfer
      TU_ASSERT(_parse_configuration_descriptor(daddr, (tusb_desc_configuration_t*) _usbh_epbuf.ctrl),);
]=])
set(TINYUSB_USBH_ENUM_CONFIG_DRIVER_PORT [=[
      usbh_device_t* dev = get_device(daddr);
      // Upstream TinyUSB:
      // TU_ASSERT(dev,);
      if (!dev) {
        (void) enum_full_complete(false);
        return;
      }

      dev->configured = 1;

      // Upstream TinyUSB:
      // // Parse configuration & set up drivers
      // // driver_open() must not make any usb transfer
      // TU_ASSERT(_parse_configuration_descriptor(daddr, (tusb_desc_configuration_t*) _usbh_epbuf.ctrl),);
      // Partial class opens are closed by the common terminal rollback below.
      uint8_t* const config_buffer = _enum_port.config_buffer ?
          _enum_port.config_buffer : _usbh_epbuf.ctrl;
      if (!_parse_configuration_descriptor(
              daddr, (tusb_desc_configuration_t*) config_buffer)) {
        (void) enum_full_complete(false);
        return;
      }

      // TinyUSB driver_open() consumes configuration bytes synchronously and
      // retains only copied interface state. Release the exact-size extension
      // before class set_config and the later Linux HID probe need heap.
      usbh_port_enum_config_release_on_host();
]=])
ergotype_tinyusb_usbh_replace_unique("configuration driver parse"
    TINYUSB_USBH_ENUM_CONFIG_DRIVER_UPSTREAM
    TINYUSB_USBH_ENUM_CONFIG_DRIVER_PORT)

set(TINYUSB_USBH_ENUM_DEFAULT_UPSTREAM [=[
    default:
      // stop enumeration if unknown state
      enum_full_complete();
      break;
]=])
set(TINYUSB_USBH_ENUM_DEFAULT_PORT [=[
    default:
      // Upstream TinyUSB:
      // // stop enumeration if unknown state
      // enum_full_complete();
      (void) enum_full_complete(false);
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("unknown enumeration state"
    TINYUSB_USBH_ENUM_DEFAULT_UPSTREAM TINYUSB_USBH_ENUM_DEFAULT_PORT)

set(TINYUSB_USBH_ENUM_TOPOLOGY_UPSTREAM [=[
  _dev0.rhport = event->rhport;
  _dev0.hub_addr = event->connection.hub_addr;
  _dev0.hub_port = event->connection.hub_port;

  if (_dev0.hub_addr == 0) {
]=])
set(TINYUSB_USBH_ENUM_TOPOLOGY_PORT [=[
  _dev0.rhport = event->rhport;
  _dev0.hub_addr = event->connection.hub_addr;
  _dev0.hub_port = event->connection.hub_port;
  // Upstream TinyUSB: no enum-progress consumer outside the host owner.
  tuh_port_enum_state_cb(_dev0.rhport, _dev0.hub_addr, _dev0.hub_port,
                         true, false);

  if (_dev0.hub_addr == 0) {
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration topology publication"
    TINYUSB_USBH_ENUM_TOPOLOGY_UPSTREAM TINYUSB_USBH_ENUM_TOPOLOGY_PORT)

set(TINYUSB_USBH_ENUM_ROOT_DEBOUNCE_UPSTREAM [=[
    hcd_port_reset(_dev0.rhport);

    // Since we are in middle of rhport reset, frame number is not available yet.
    // need to depend on tusb_time_millis_api()
    tusb_time_delay_ms_api(ENUM_RESET_DELAY_MS);

    hcd_port_reset_end(_dev0.rhport);

    // wait until device connection is stable TODO non blocking
    tusb_time_delay_ms_api(ENUM_DEBOUNCING_DELAY_MS);

    // device unplugged while delaying
    if (!hcd_port_connect_status(_dev0.rhport)) {
      enum_full_complete();
      return true;
    }

    _dev0.speed = hcd_port_speed_get(_dev0.rhport);
    TU_LOG_USBH("%s Speed\r\n", tu_str_speed[_dev0.speed]);

    // fake transfer to kick-off the enumeration process
    tuh_xfer_t xfer;
    xfer.daddr = 0;
    xfer.result = XFER_RESULT_SUCCESS;
    xfer.user_data = ENUM_ADDR0_DEVICE_DESC;

    process_enumeration(&xfer);
]=])
set(TINYUSB_USBH_ENUM_ROOT_DEBOUNCE_PORT [=[
    hcd_port_reset(_dev0.rhport);

    // Upstream TinyUSB:
    // // Since we are in middle of rhport reset, frame number is not available yet.
    // // need to depend on tusb_time_millis_api()
    // tusb_time_delay_ms_api(ENUM_RESET_DELAY_MS);
    //
    // hcd_port_reset_end(_dev0.rhport);
    //
    // // wait until device connection is stable TODO non blocking
    // tusb_time_delay_ms_api(ENUM_DEBOUNCING_DELAY_MS);
    //
    // // device unplugged while delaying
    // if (!hcd_port_connect_status(_dev0.rhport)) {
    //   enum_full_complete();
    //   return true;
    // }
    //
    // _dev0.speed = hcd_port_speed_get(_dev0.rhport);
    // TU_LOG_USBH("%s Speed\r\n", tu_str_speed[_dev0.speed]);
    //
    // // fake transfer to kick-off the enumeration process
    // tuh_xfer_t xfer;
    // xfer.daddr = 0;
    // xfer.result = XFER_RESULT_SUCCESS;
    // xfer.user_data = ENUM_ADDR0_DEVICE_DESC;
    //
    // process_enumeration(&xfer);
    // Current TinyUSB uses asynchronous delayed continuations for both reset
    // and debounce. Keep the event pump runnable and preserve the pinned
    // reset-then-debounce ordering across the firmware host owner.
    _enum_port.progress_ms = tusb_time_millis_api();
    _enum_port.delay_state = USBH_PORT_ENUM_DELAY_ROOT_RESET;
]=])
ergotype_tinyusb_usbh_replace_unique("asynchronous root timing"
    TINYUSB_USBH_ENUM_ROOT_DEBOUNCE_UPSTREAM
    TINYUSB_USBH_ENUM_ROOT_DEBOUNCE_PORT)

set(TINYUSB_USBH_ENUM_HUB_DEBOUNCE_UPSTREAM [=[
    // wait until device connection is stable TODO non blocking
    tusb_time_delay_ms_api(ENUM_DEBOUNCING_DELAY_MS);

    // ENUM_HUB_GET_STATUS
    TU_ASSERT(hub_port_get_status(_dev0.hub_addr, _dev0.hub_port, _usbh_epbuf.ctrl,
                                  process_enumeration, ENUM_HUB_CLEAR_RESET_1));
]=])
set(TINYUSB_USBH_ENUM_HUB_DEBOUNCE_PORT [=[
    // Upstream TinyUSB:
    // // wait until device connection is stable TODO non blocking
    // tusb_time_delay_ms_api(ENUM_DEBOUNCING_DELAY_MS);
    //
    // // ENUM_HUB_GET_STATUS
    // TU_ASSERT(hub_port_get_status(_dev0.hub_addr, _dev0.hub_port, _usbh_epbuf.ctrl,
    //                               process_enumeration, ENUM_HUB_CLEAR_RESET_1));
    // Current TinyUSB uses an asynchronous delayed continuation here. The hub
    // status request is submitted by the same host owner after the deadline.
    uint32_t const debounce_now = tusb_time_millis_api();
    _enum_port.progress_ms = debounce_now;
    _enum_port.delay_state = USBH_PORT_ENUM_DELAY_DEBOUNCE;
]=])
ergotype_tinyusb_usbh_replace_unique("asynchronous hub debounce"
    TINYUSB_USBH_ENUM_HUB_DEBOUNCE_UPSTREAM
    TINYUSB_USBH_ENUM_HUB_DEBOUNCE_PORT)

set(TINYUSB_USBH_ENUM_PUBLISH_ADDRESS_UPSTREAM [=[
  new_dev->connected = 1;
  new_dev->ep0_size = desc_device->bMaxPacketSize0;

  tusb_control_request_t const request = {
]=])
set(TINYUSB_USBH_ENUM_PUBLISH_ADDRESS_PORT [=[
  new_dev->connected = 1;
  new_dev->ep0_size = desc_device->bMaxPacketSize0;
  // Upstream TinyUSB: no exact provisional owner. Publish before SET_ADDRESS
  // so its address-zero ACK and every later EP0 stage share one enum epoch.
  _enum_port.daddr = new_addr;

  tusb_control_request_t const request = {
]=])
ergotype_tinyusb_usbh_replace_unique("provisional address owner"
    TINYUSB_USBH_ENUM_PUBLISH_ADDRESS_UPSTREAM
    TINYUSB_USBH_ENUM_PUBLISH_ADDRESS_PORT)

set(TINYUSB_USBH_DRIVER_CONFIG_GUARD_UPSTREAM [=[
void usbh_driver_set_config_complete(uint8_t dev_addr, uint8_t itf_num) {
  usbh_device_t* dev = get_device(dev_addr);
]=])
set(TINYUSB_USBH_DRIVER_CONFIG_GUARD_PORT [=[
void usbh_driver_set_config_complete(uint8_t dev_addr, uint8_t itf_num) {
  // Upstream TinyUSB: no exact enumeration owner. Ignore a delayed class
  // continuation after timeout/REMOVE cleanup instead of mounting cleared state.
  if (!_dev0.enumerating || _enum_port.aborting ||
      _enum_port.daddr != dev_addr) return;
  usbh_device_t* dev = get_device(dev_addr);
]=])
ergotype_tinyusb_usbh_replace_unique("class configuration owner guard"
    TINYUSB_USBH_DRIVER_CONFIG_GUARD_UPSTREAM
    TINYUSB_USBH_DRIVER_CONFIG_GUARD_PORT)

set(TINYUSB_USBH_DRIVER_SET_CONFIG_UPSTREAM [=[
      TU_LOG_USBH("%s set config: itf = %u\r\n", driver->name, itf_num);
      driver->set_config(dev_addr, itf_num);
      break;
]=])
set(TINYUSB_USBH_DRIVER_SET_CONFIG_PORT [=[
      TU_LOG_USBH("%s set config: itf = %u\r\n", driver->name, itf_num);
      // Upstream TinyUSB:
      // driver->set_config(dev_addr, itf_num);
      // A class that rejects its first synchronous step cannot ever call the
      // continuation; close every partially opened interface immediately.
      if (!driver->set_config(dev_addr, itf_num)) {
        (void) enum_full_complete(false);
        return;
      }
      break;
]=])
ergotype_tinyusb_usbh_replace_unique("class set-config submission"
    TINYUSB_USBH_DRIVER_SET_CONFIG_UPSTREAM
    TINYUSB_USBH_DRIVER_SET_CONFIG_PORT)

set(TINYUSB_USBH_ENUM_TERMINAL_UPSTREAM [=[
static void enum_full_complete(void) {
  // mark enumeration as complete
  _dev0.enumerating = 0;

#if CFG_TUH_HUB
  // get next hub status
  if (_dev0.hub_addr) hub_edpt_status_xfer(_dev0.hub_addr);
#endif

}
]=])
set(TINYUSB_USBH_ENUM_TERMINAL_PORT [=[
// Upstream TinyUSB: current releases carry an equivalent delayed-enumeration
// state machine. The pinned release blocks inside enum_new_device() and the
// SET_ADDRESS completion, so the firmware host owner resumes those exact tails.
static uint32_t usbh_port_enum_delay_ms_on_host(uint8_t state) {
  if (state == USBH_PORT_ENUM_DELAY_ROOT_RESET)
    return ENUM_RESET_DELAY_MS;
  if (state == USBH_PORT_ENUM_DELAY_DEBOUNCE)
    return ENUM_DEBOUNCING_DELAY_MS;
  if (state == USBH_PORT_ENUM_DELAY_RETRY)
    return USBH_PORT_ENUM_RETRY_MS;
  return 2;
}

static uint32_t usbh_port_enum_delay_deadline_on_host(uint8_t state) {
  // Current TinyUSB adds one millisecond so a millisecond-quantized clock cannot
  // resume just short of the requested physical delay.
  return _enum_port.progress_ms +
         usbh_port_enum_delay_ms_on_host(state) + 1;
}

static void usbh_port_enum_cancel_root_reset_on_host(void) {
  if (_enum_port.delay_state != USBH_PORT_ENUM_DELAY_ROOT_RESET) return;
  _enum_port.delay_state = USBH_PORT_ENUM_DELAY_NONE;
  hcd_port_reset_end(_dev0.rhport);
}

static void usbh_port_enum_delay_complete_on_host(uint32_t now) {
  uint8_t const state = _enum_port.delay_state;
  _enum_port.delay_state = USBH_PORT_ENUM_DELAY_NONE;
  _enum_port.progress_ms = now;

  if (state == USBH_PORT_ENUM_DELAY_RETRY) {
    // Upstream TinyUSB:
    // tusb_time_delay_ms_api(ATTEMPT_DELAY_MS); // delay a bit
    // TU_LOG1("Enumeration attempt %u\r\n", failed_count);
    // retry = tuh_control_xfer(xfer);
    // The host task owns both the deadline and EP0. Rebuild the callback-local
    // transfer only after the shared control owner is idle.
    tuh_xfer_t retry_xfer = {
      .daddr = _enum_port.retry_daddr,
      .ep_addr = 0,
      .setup = &_enum_port.retry_request,
      .buffer = _enum_port.retry_buffer,
      .complete_cb = process_enumeration,
      .user_data = _enum_port.retry_user_data
    };

    TU_LOG1("Enumeration attempt %u\r\n", _enum_port.failed_count);
    if (!tuh_control_xfer(&retry_xfer))
      (void) enum_full_complete(false);
    return;
  }

  if (state == USBH_PORT_ENUM_DELAY_ROOT_RESET) {
    hcd_port_reset_end(_dev0.rhport);
    _enum_port.delay_state = USBH_PORT_ENUM_DELAY_DEBOUNCE;
    return;
  }

  if (state == USBH_PORT_ENUM_DELAY_DEBOUNCE &&
      _dev0.hub_addr == 0) {
    if (!hcd_port_connect_status(_dev0.rhport)) {
      _enum_port.removed = true;
      _enum_port.removed_hub_addr = 0;
      _enum_port.removed_hub_port = 0;
      _enum_port.removed_scope_rank =
          (uint8_t) (2 * TOTAL_DEVICES + 1);
      (void) enum_full_complete(false);
      return;
    }

    _dev0.speed = hcd_port_speed_get(_dev0.rhport);
    TU_LOG_USBH("%s Speed\r\n", tu_str_speed[_dev0.speed]);

    tuh_xfer_t xfer;
    xfer.daddr = 0;
    xfer.result = XFER_RESULT_SUCCESS;
    xfer.user_data = ENUM_ADDR0_DEVICE_DESC;
    process_enumeration(&xfer);
    return;
  }

#if CFG_TUH_HUB
  if (state == USBH_PORT_ENUM_DELAY_DEBOUNCE) {
    if (!hub_port_get_status(_dev0.hub_addr, _dev0.hub_port,
                             _usbh_epbuf.ctrl, process_enumeration,
                             ENUM_HUB_CLEAR_RESET_1)) {
      (void) enum_full_complete(false);
    }
    return;
  }
#endif

  uint8_t const new_addr = _enum_port.daddr;
  usbh_device_t* new_dev = get_device(new_addr);
  new_dev->addressed = 1;

  hcd_device_close(_dev0.rhport, 0);
  if (!usbh_edpt_control_open(new_addr, new_dev->ep0_size)) {
    (void) enum_full_complete(false);
    return;
  }

  TU_LOG_USBH("Get Device Descriptor\r\n");
  if (!tuh_descriptor_get_device(new_addr, _usbh_epbuf.ctrl,
                                 sizeof(tusb_desc_device_t),
                                 process_enumeration,
                                 ENUM_GET_9BYTE_CONFIG_DESC)) {
    (void) enum_full_complete(false);
  }
}

// Upstream TinyUSB:
// static void enum_full_complete(void) {
//   // mark enumeration as complete
//   _dev0.enumerating = 0;
//
// #if CFG_TUH_HUB
//   // get next hub status
//   if (_dev0.hub_addr) hub_edpt_status_xfer(_dev0.hub_addr);
// #endif
//
// }
// Host-only terminal fence modelled after current TinyUSB's successful/failed
// enum completion split. Failure closes address zero, every partially opened
// class, and the provisional device before a deferred ATTACH can reuse state.
static bool usbh_port_enum_cleanup_contains_control_on_host(void) {
  if (_ctrl_xfer.stage == CONTROL_STAGE_IDLE || _ctrl_xfer.daddr == 0)
    return false;
  uint8_t const cleanup_hub_addr =
      _enum_port.removed ? _enum_port.removed_hub_addr : _dev0.hub_addr;
  uint8_t const cleanup_hub_port =
      _enum_port.removed ? _enum_port.removed_hub_port : _dev0.hub_port;
  return usbh_port_scope_contains_device_on_host(
      cleanup_hub_addr, cleanup_hub_port, _ctrl_xfer.daddr);
}

// Upstream TinyUSB: no bounded configuration preflight. Its parser advances by
// each device-supplied bLength and casts interface/IAD/endpoint descriptors.
// Exact-size heap storage makes an invalid final descriptor an immediate OOB,
// so reject malformed streams and interface-array overflow before parser entry.
static bool usbh_port_enum_config_has_interfaces_on_host(
    uint8_t const* config, uint16_t total_len, uint16_t offset,
    uint8_t count, uint8_t interface_len) {
  uint8_t found = 0;

  // The caller has already validated every bLength in this stream.
  while (offset < total_len) {
    uint8_t const* desc = config + offset;
    if (desc[1] == TUSB_DESC_INTERFACE_ASSOCIATION) return false;
    if (desc[1] == TUSB_DESC_INTERFACE &&
        ((tusb_desc_interface_t const*) desc)->bAlternateSetting == 0) {
      // TinyUSB accounts every member with the first interface's bLength.
      // Larger headers are safe only when that group keeps one stride.
      if (desc[0] != interface_len) return false;
      found++;
      if (found == count) return true;
    }
    offset = (uint16_t) (offset + desc[0]);
  }
  return false;
}

static bool usbh_port_enum_config_valid_on_host(uint8_t const* config,
                                                uint16_t total_len) {
  if (total_len < sizeof(tusb_desc_configuration_t) ||
      config[0] != sizeof(tusb_desc_configuration_t) ||
      config[1] != TUSB_DESC_CONFIGURATION)
    return false;

  tusb_desc_configuration_t const* desc_cfg =
      (tusb_desc_configuration_t const*) config;
  uint16_t const declared_len = tu_le16toh(tu_unaligned_read16(
      config + offsetof(tusb_desc_configuration_t, wTotalLength)));
  if (declared_len != total_len ||
      desc_cfg->bNumInterfaces > CFG_TUH_INTERFACE_MAX)
    return false;

  uint16_t offset = config[0];
  uint16_t interface_count = 0;
  bool first_child = true;
  while (offset < total_len) {
    uint16_t const remaining = (uint16_t) (total_len - offset);
    if (remaining < 2) return false;

    uint8_t const* desc = config + offset;
    uint8_t const desc_len = desc[0];
    uint8_t const desc_type = desc[1];
    if (desc_len < 2 || desc_len > remaining) return false;
    if (first_child && desc_type != TUSB_DESC_INTERFACE_ASSOCIATION &&
        desc_type != TUSB_DESC_INTERFACE)
      return false;

    if (desc_type == TUSB_DESC_INTERFACE_ASSOCIATION) {
      if (desc_len < sizeof(tusb_desc_interface_assoc_t)) return false;

      tusb_desc_interface_assoc_t const* iad =
          (tusb_desc_interface_assoc_t const*) desc;
      uint8_t const count = iad->bInterfaceCount;
      if (!count || iad->bFirstInterface >= CFG_TUH_INTERFACE_MAX ||
          count > CFG_TUH_INTERFACE_MAX - iad->bFirstInterface)
        return false;

      uint16_t const next_offset = (uint16_t) (offset + desc_len);
      uint16_t const next_remaining =
          (uint16_t) (total_len - next_offset);
      if (next_remaining < sizeof(tusb_desc_interface_t)) return false;

      uint8_t const* next = config + next_offset;
      if (next[0] < sizeof(tusb_desc_interface_t) ||
          next[0] > next_remaining || next[1] != TUSB_DESC_INTERFACE ||
          ((tusb_desc_interface_t const*) next)->bInterfaceNumber !=
              iad->bFirstInterface ||
          ((tusb_desc_interface_t const*) next)->bAlternateSetting != 0)
        return false;
    } else if (desc_type == TUSB_DESC_INTERFACE) {
      if (desc_len < sizeof(tusb_desc_interface_t)) return false;

      tusb_desc_interface_t const* itf =
          (tusb_desc_interface_t const*) desc;
      if (itf->bInterfaceNumber >= CFG_TUH_INTERFACE_MAX)
        return false;
      if (first_child && itf->bAlternateSetting != 0) return false;
      if (itf->bAlternateSetting == 0) {
        interface_count++;
        if (interface_count > desc_cfg->bNumInterfaces) return false;
      }
    } else if (desc_type == TUSB_DESC_ENDPOINT &&
               desc_len < sizeof(tusb_desc_endpoint_t)) {
      return false;
    }

    offset = (uint16_t) (offset + desc_len);
    first_child = false;
  }

  if (offset != total_len || interface_count != desc_cfg->bNumInterfaces)
    return false;

  // TinyUSB groups IAD interfaces, plus legacy CDC/MIDI pairs without an IAD,
  // before driver_open(). Prove every requested base interface exists so its
  // helper cannot step from the exact-size buffer end looking for another one.
  offset = config[0];
  while (offset < total_len) {
    uint8_t const* desc = config + offset;
    uint8_t const desc_type = desc[1];
    if (desc_type == TUSB_DESC_INTERFACE_ASSOCIATION) {
      tusb_desc_interface_assoc_t const* iad =
          (tusb_desc_interface_assoc_t const*) desc;
      if (!usbh_port_enum_config_has_interfaces_on_host(
              config, total_len, (uint16_t) (offset + desc[0]),
              iad->bInterfaceCount, config[offset + desc[0]]))
        return false;
    } else if (desc_type == TUSB_DESC_INTERFACE) {
      tusb_desc_interface_t const* itf =
          (tusb_desc_interface_t const*) desc;
      bool implicit_pair = false;
#if CFG_TUH_MIDI
      implicit_pair = TUSB_CLASS_AUDIO == itf->bInterfaceClass &&
                      AUDIO_SUBCLASS_CONTROL == itf->bInterfaceSubClass &&
                      AUDIO_FUNC_PROTOCOL_CODE_UNDEF ==
                          itf->bInterfaceProtocol;
#endif
#if CFG_TUH_CDC
      implicit_pair = implicit_pair ||
                      (TUSB_CLASS_CDC == itf->bInterfaceClass &&
                       CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL ==
                           itf->bInterfaceSubClass);
#endif
      if (implicit_pair && itf->bAlternateSetting == 0) {
        if (itf->bInterfaceNumber > CFG_TUH_INTERFACE_MAX - 2 ||
            !usbh_port_enum_config_has_interfaces_on_host(
                config, total_len, offset, 2, desc[0]))
          return false;
      }
    }
    offset = (uint16_t) (offset + desc[0]);
  }

  return true;
}

// Upstream TinyUSB: no equivalent; its configuration descriptor always lives
// in the permanent enumeration scratch. The port's exact-size extension owns
// one transient host-transfer buffer through parse, retry, and terminal drain.
static void usbh_port_enum_config_release_on_host(void) {
  uint8_t* const config_buffer = _enum_port.config_buffer;
  if (config_buffer) {
    if (_enum_port.retry_buffer == config_buffer)
      _enum_port.retry_buffer = NULL;
    tuh_port_enum_buffer_free_on_host(config_buffer);
  }
  _enum_port.config_buffer = NULL;
  _enum_port.config_len = 0;
  _enum_port.config_fetch_pending = false;
}

static bool enum_full_complete(bool success) {
  uint8_t const rhport = _dev0.rhport;
  uint8_t const hub_addr = _dev0.hub_addr;
  uint8_t const hub_port = _dev0.hub_port;
  bool const removed = _enum_port.removed;
  bool const timed_out = _enum_port.timed_out;
  bool const restart = _enum_port.restart_pending;
  uint8_t const cleanup_hub_addr =
      removed ? _enum_port.removed_hub_addr : hub_addr;
  uint8_t const cleanup_hub_port =
      removed ? _enum_port.removed_hub_port : hub_port;
  bool const enum_parent_survives =
      !removed || _enum_port.removed_hub_addr == hub_addr;

  if (!_dev0.enumerating) return false;
  usbh_port_enum_cancel_root_reset_on_host();
  if (success && _enum_port.aborting) success = false;

  // Never clear or abort a foreign EP0. Exact enum EP0 drains physically;
  // a removed all-tree scope gets its own queue/SOF fence before normal close.
  if (!success && _ctrl_xfer.stage != CONTROL_STAGE_IDLE) {
    bool const owned = usbh_port_enum_control_owned_on_host();
    bool const foreign_in_cleanup =
        !owned && usbh_port_enum_cleanup_contains_control_on_host();
    if (owned || restart || (foreign_in_cleanup && !_enum_port.foreign_fenced)) {
      if (!_enum_port.aborting) {
        _enum_port.aborting = true;
        _enum_port.drain_passes = 0;
        _enum_port.drain_at_ms = tusb_time_millis_api();
        _enum_port.drain_event_start = 0;
        _enum_port.drain_started = false;
        _enum_port.drain_queue_fencing = false;
      }
      return false;
    }
  }

  _dev0.enumerating = 0;
  if (!success) {
    hcd_device_close(rhport, 0);
    process_removing_device(rhport, cleanup_hub_addr, cleanup_hub_port);
    if (_enum_port.report_event == USBH_PORT_ENUM_EVENT_NONE) {
      if (timed_out)
        _enum_port.report_event = USBH_PORT_ENUM_EVENT_TIMEOUT;
      else if (!removed && !restart)
        _enum_port.report_event = USBH_PORT_ENUM_EVENT_FAILED;
    }
  }

  usbh_port_enum_config_release_on_host();

  _enum_port.progress_ms = 0;
  _enum_port.drain_at_ms = 0;
  _enum_port.drain_event_start = 0;
  _enum_port.retry_buffer = NULL;
  _enum_port.retry_user_data = 0;
  _enum_port.daddr = 0;
  _enum_port.retry_daddr = 0;
  _enum_port.failed_count = 0;
  _enum_port.drain_passes = 0;
  _enum_port.removed_hub_addr = 0;
  _enum_port.removed_hub_port = 0;
  _enum_port.removed_scope_rank = 0;
  _enum_port.aborting = false;
  _enum_port.delay_state = USBH_PORT_ENUM_DELAY_NONE;
  _enum_port.removed = false;
  _enum_port.timed_out = false;
  _enum_port.restart_pending = false;
  _enum_port.foreign_fenced = false;
  _enum_port.drain_started = false;
  _enum_port.drain_queue_fencing = false;

  if (!restart) {
    tuh_port_enum_state_cb(rhport, hub_addr, hub_port, false, success);
  }

#if CFG_TUH_HUB
  if (!restart && hub_addr && enum_parent_survives)
    (void) hub_edpt_status_xfer(hub_addr);
#endif

  if (restart) {
    hcd_event_t event = {
      .rhport = rhport,
      .event_id = HCD_EVENT_DEVICE_ATTACH,
      .connection = {
        .hub_addr = hub_addr,
        .hub_port = hub_port
      }
    };
    _dev0.enumerating = 1;
    usbh_port_enum_start_on_host();
    (void) enum_new_device(&event);
  }

  return success;
}

static bool usbh_port_time_reached(uint32_t now, uint32_t deadline) {
  return (int32_t) (now - deadline) >= 0;
}

static uint32_t usbh_port_wait_until_on_host(uint32_t now,
                                             uint32_t deadline) {
  int32_t const remaining = (int32_t) (deadline - now);
  return remaining > 0 ? (uint32_t) remaining : 0;
}

// Current TinyUSB shortens its blocking host-queue receive to the nearest
// delayed-call deadline. Derive the same one-shot wait from the pinned enum
// state: real HCD/deferred events still wake the queue immediately, and idle
// has no periodic timeout.
static uint32_t usbh_port_enum_next_wait_ms_on_host(uint32_t now) {
  if (_enum_port.report_event != USBH_PORT_ENUM_EVENT_NONE ||
      _enum_port.attach_overflow_pending)
    return 0;

  if (!_dev0.enumerating) {
    if (_enum_deferred_attach_count &&
        _ctrl_xfer.stage == CONTROL_STAGE_IDLE)
      return 0;
    return OSAL_TIMEOUT_WAIT_FOREVER;
  }

  if (!_enum_port.aborting) {
    if (_enum_port.config_fetch_pending &&
        _ctrl_xfer.stage == CONTROL_STAGE_IDLE)
      return 0;

    uint32_t wait_ms = usbh_port_wait_until_on_host(
        now, _enum_port.progress_ms + USBH_PORT_ENUM_WATCHDOG_MS);
    uint8_t const state = _enum_port.delay_state;
    if (state != USBH_PORT_ENUM_DELAY_NONE &&
        (state == USBH_PORT_ENUM_DELAY_ROOT_RESET ||
         _ctrl_xfer.stage == CONTROL_STAGE_IDLE)) {
      uint32_t const delay_wait_ms = usbh_port_wait_until_on_host(
          now, usbh_port_enum_delay_deadline_on_host(state));
      if (delay_wait_ms < wait_ms) wait_ms = delay_wait_ms;
    }
    return wait_ms;
  }

  if (_ctrl_xfer.stage == CONTROL_STAGE_IDLE) return 0;
  if (usbh_port_enum_control_owned_on_host()) {
    if (!_enum_port.drain_started) return 0;
    return usbh_port_wait_until_on_host(now, _enum_port.drain_at_ms);
  }
  if (_enum_port.restart_pending) return OSAL_TIMEOUT_WAIT_FOREVER;
  if (!usbh_port_enum_cleanup_contains_control_on_host()) return 0;
  if (_enum_port.removed) {
    if (!_enum_port.drain_started) return 0;
    return usbh_port_wait_until_on_host(now, _enum_port.drain_at_ms);
  }
  return OSAL_TIMEOUT_WAIT_FOREVER;
}

// Upstream TinyUSB: no physical HCD-to-host-queue retirement fence. PIO USB's
// endpoint abort can meet a transfer already owned by the current 1-ms frame;
// pio_usb_host_frame() then publishes ep_complete/error/stall and its IRQ queues
// and clears those bits at frame end. After two milliseconds, either an empty
// FIFO or one full FIFO-capacity of later dequeues proves every event which was
// publishable at the deadline has crossed this host owner, even when unrelated
// HCD traffic keeps the queue continuously non-empty.
static bool usbh_port_enum_drain_fenced_on_host(uint32_t now) {
  if (!_enum_port.drain_started ||
      !usbh_port_time_reached(now, _enum_port.drain_at_ms)) return false;

  if (!_enum_port.drain_queue_fencing) {
    _enum_port.drain_event_start = _usbh_port_event_count;
    _enum_port.drain_queue_fencing = true;
  }

  return osal_queue_empty(_usbh_q) ||
         (uint32_t) (_usbh_port_event_count -
                     _enum_port.drain_event_start) >=
             (uint32_t) CFG_TUH_TASK_QUEUE_SZ;
}

// Upstream TinyUSB: no no-progress deadline for the pinned enumeration state.
// Called only at TinyUSB task entry, after each bounded event-drain pass or
// exact queue deadline. It returns the next internal queue wait; there is no
// fixed polling interval.
static int usbh_port_enum_service_on_host(uint32_t* wait_ms) {
  uint32_t const now = tusb_time_millis_api();
  uint8_t const delay_state = _enum_port.delay_state;

  if (_dev0.enumerating && !_enum_port.aborting &&
      usbh_port_time_reached(now,
                             _enum_port.progress_ms +
                             USBH_PORT_ENUM_WATCHDOG_MS)) {
    _enum_port.aborting = true;
    _enum_port.timed_out = true;
    _enum_port.drain_passes = 0;
    _enum_port.drain_at_ms = now;
    _enum_port.drain_event_start = 0;
    _enum_port.drain_started = false;
    _enum_port.drain_queue_fencing = false;
  }

  if (_dev0.enumerating && !_enum_port.aborting &&
      delay_state != USBH_PORT_ENUM_DELAY_NONE &&
      usbh_port_time_reached(
          now, usbh_port_enum_delay_deadline_on_host(delay_state)) &&
      (delay_state == USBH_PORT_ENUM_DELAY_ROOT_RESET ||
       _ctrl_xfer.stage == CONTROL_STAGE_IDLE)) {
    usbh_port_enum_delay_complete_on_host(now);
  }

  if (_dev0.enumerating && !_enum_port.aborting &&
      _enum_port.config_fetch_pending &&
      _ctrl_xfer.stage == CONTROL_STAGE_IDLE) {
    uint16_t const config_len = _enum_port.config_len;
    uint8_t* const config_buffer =
        tuh_port_enum_buffer_alloc_on_host(config_len);
    if (!config_buffer) {
      _enum_port.report_event = USBH_PORT_ENUM_EVENT_CONFIG_NOMEM;
      (void) enum_full_complete(false);
    } else {
      uint8_t const config_idx = CONFIG_NUM - 1;

      _enum_port.config_buffer = config_buffer;
      _enum_port.config_fetch_pending = false;
      _enum_port.progress_ms = now;
      if (!tuh_descriptor_get_configuration(
              _enum_port.daddr, config_idx, config_buffer, config_len,
              process_enumeration, ENUM_SET_CONFIG)) {
        (void) enum_full_complete(false);
      }
    }
  }

  if (_dev0.enumerating && _enum_port.aborting) {
    usbh_port_enum_cancel_root_reset_on_host();
    if (_ctrl_xfer.stage == CONTROL_STAGE_IDLE) {
      (void) enum_full_complete(false);
    } else if (usbh_port_enum_control_owned_on_host()) {
      if (!_enum_port.drain_started) {
        (void) hcd_edpt_abort_xfer(usbh_get_rhport(_ctrl_xfer.daddr),
                                   _ctrl_xfer.daddr, 0);
        _enum_port.drain_started = true;
        _enum_port.drain_queue_fencing = false;
        // Current TinyUSB delayed calls add one millisecond to guarantee the full
        // physical interval on a millisecond-quantized clock.
        _enum_port.drain_at_ms = now + USBH_PORT_ENUM_DRAIN_MS + 1;
        goto enum_service_report;
      }
      if (!usbh_port_enum_drain_fenced_on_host(now))
        goto enum_service_report;

      _enum_port.drain_passes++;
      _enum_port.drain_started = false;
      _enum_port.drain_queue_fencing = false;
      if (_enum_port.drain_passes >= USBH_PORT_ENUM_DRAIN_PASSES) {
        // SETUP, DATA, and ACK are the only physical EP0 stages. After three
        // abort/delay/dequeue fences no completion from this epoch remains.
        _set_control_xfer_stage(CONTROL_STAGE_IDLE);
        (void) enum_full_complete(false);
      }
    } else if (_enum_port.restart_pending) {
      // A fresh enumeration cannot start while an unrelated EP0 owns the one
      // global control pipe. Its own timeout/recovery will eventually release it.
    } else if (!usbh_port_enum_cleanup_contains_control_on_host()) {
      // Child rollback cannot touch this sibling/parent owner.
      (void) enum_full_complete(false);
    } else if (_enum_port.removed) {
      // An ancestor/root REMOVE legitimately closes the whole subtree. Do not
      // abort its foreign EP0; use the same PIO/FIFO retirement proof before
      // process_removing_device() performs the ordinary physical-remove close.
      if (!_enum_port.drain_started) {
        _enum_port.drain_started = true;
        _enum_port.drain_queue_fencing = false;
        // Current TinyUSB delayed calls add one millisecond to guarantee the full
        // physical interval on a millisecond-quantized clock.
        _enum_port.drain_at_ms = now + USBH_PORT_ENUM_DRAIN_MS + 1;
        goto enum_service_report;
      }
      if (!usbh_port_enum_drain_fenced_on_host(now))
        goto enum_service_report;

      _enum_port.drain_passes++;
      _enum_port.drain_started = false;
      _enum_port.drain_queue_fencing = false;
      if (_enum_port.drain_passes >= USBH_PORT_ENUM_DRAIN_PASSES) {
        _enum_port.foreign_fenced = true;
        (void) enum_full_complete(false);
      }
    }
  }

  if (!_dev0.enumerating && _ctrl_xfer.stage == CONTROL_STAGE_IDLE) {
    // Current TinyUSB drains its deferred-attach queue on the next host-loop
    // iteration after enumeration becomes idle. Do the same from this host
    // service, after the previous terminal's mount callback has fully unwound.
    hcd_event_t event;
    if (usbh_port_take_deferred_attach_on_host(&event)) {
      _dev0.enumerating = 1;
      usbh_port_enum_start_on_host();
      (void) enum_new_device(&event);
    }
  }

enum_service_report: ;
  int report_event = _enum_port.report_event;
  if (report_event != USBH_PORT_ENUM_EVENT_NONE) {
    _enum_port.report_event = USBH_PORT_ENUM_EVENT_NONE;
  } else if (_enum_port.attach_overflow_pending) {
    // Do not let a queue-capacity diagnostic replace a terminal timeout/fail.
    _enum_port.attach_overflow_pending = false;
    report_event = USBH_PORT_ENUM_EVENT_ATTACH_OVERFLOW;
  }
  *wait_ms = usbh_port_enum_next_wait_ms_on_host(tusb_time_millis_api());
  return report_event;
}
]=])
ergotype_tinyusb_usbh_replace_unique("enumeration terminal and watchdog"
    TINYUSB_USBH_ENUM_TERMINAL_UPSTREAM TINYUSB_USBH_ENUM_TERMINAL_PORT)
