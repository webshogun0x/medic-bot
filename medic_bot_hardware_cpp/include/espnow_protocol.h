#ifndef ESPNOW_PROTOCOL_H
#define ESPNOW_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_MAGIC_BYTE 0x4D // 'M'

typedef enum {
    NODE_MAIN_CONTROLLER = 0x00,
    NODE_WEIGHT_SCALE    = 0x01,
    NODE_HEIGHT_SONAR    = 0x02,
    NODE_STEPPER_GANTRY  = 0x03,
    NODE_TEMP_CARRIAGE   = 0x04
} espnow_node_id_t;

typedef enum {
    CMD_PING             = 0x10,
    CMD_GET_WEIGHT       = 0x20,
    CMD_GET_HEIGHT       = 0x30,
    CMD_GET_GANTRY_POS   = 0x35,
    CMD_MOVE_CARRIAGE    = 0x40,
    CMD_MEASURE_TEMP     = 0x50,
    CMD_RETURN_HOME      = 0x60,
    CMD_EMERGENCY_HALT   = 0xEE,
    RESP_WEIGHT          = 0x81,
    RESP_HEIGHT          = 0x82,
    RESP_STEPPER_ACK     = 0x83,
    RESP_TEMP            = 0x84,
    RESP_GANTRY_POS      = 0x85,
    RESP_ERROR           = 0xFF
} espnow_cmd_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;         // 0x4D ('M')
    uint8_t src_node;      // espnow_node_id_t
    uint8_t dest_node;     // espnow_node_id_t
    uint8_t opcode;        // espnow_cmd_t
    uint16_t seq;          // Rolling sequence counter
    uint8_t status;        // 0 = OK, >0 = Error code
    uint8_t reserved;
    union {
        float weight;
        float height;
        float temperature;
        struct {
            float height_sonar_cm;
            float height_laser_cm;
        } dual_height;
        struct {
            float body_temp;
            float ambient_temp;
        } dual_temp;
        struct {
            float target_height_cm;
            uint16_t speed;
            uint16_t microsteps;
        } stepper_cmd;
        struct {
            float current_position_cm;
            uint8_t aligned;
        } gantry_pos;
        uint8_t raw_payload[16];
    } data;
} espnow_kiosk_packet_t;

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_PROTOCOL_H
