// message.c (최종 확인용)
#include "../include/message.h"
#include "../include/reservation.h"
#include "../include/network.h"

static Message* create_message_with_two_args(MessageType type, const char* arg1, const char* arg2) {
    Message* msg = create_message(type, NULL);
    if (!msg) return NULL;
    
    msg->args[0] = strdup(arg1);
    msg->args[1] = strdup(arg2);
    if (!msg->args[0] || !msg->args[1]) {
        destroy_message(msg);
        return NULL;
    }
    msg->arg_count = 2;
    return msg;
}

Message* create_message(MessageType type, const char *data) {
    Message *msg = (Message *)malloc(sizeof(Message));
    if (!msg) {
        utils_report_error(ERROR_MESSAGE_CREATION_FAILED, "Message", "메시지 메모리 할당 실패");
        return NULL;
    }
    msg->type = type;
    msg->arg_count = 0;
    memset(msg->args, 0, sizeof(msg->args));
    if (data) {
        strncpy(msg->data, data, MAX_MESSAGE_LENGTH - 1);
        msg->data[MAX_MESSAGE_LENGTH - 1] = '\0';
    } else {
        msg->data[0] = '\0';
    }
    return msg;
}

bool fill_status_response_args(Message* msg, const Device* devices, int count, ResourceManager* rm, ReservationManager* rvm) {
    if (!msg || !devices || !rm || !rvm) return false;
    msg->arg_count = 0;
    for (int i = 0; i < count; i++) {
        int base_idx = i * 6;
        if (base_idx + 5 >= MAX_ARGS) break;
        const char* status_str = get_device_status_string(devices[i].status);
        char end_time_str[32] = "0";
        char username_str[MAX_USERNAME_LENGTH] = "";
        if (devices[i].status == DEVICE_RESERVED) {
            Reservation* res = get_active_reservation_for_device(rvm, rm, devices[i].id);
            if (res) {
                snprintf(end_time_str, sizeof(end_time_str), "%ld", res->end_time);
                strncpy(username_str, res->username, sizeof(username_str) - 1);
                username_str[sizeof(username_str) - 1] = '\0';
            }
        }
        msg->args[base_idx]     = strdup(devices[i].id);
        msg->args[base_idx + 1] = strdup(devices[i].name);
        msg->args[base_idx + 2] = strdup(devices[i].type);
        msg->args[base_idx + 3] = strdup(status_str);
        msg->args[base_idx + 4] = strdup(end_time_str);
        msg->args[base_idx + 5] = strdup(username_str);
        if (!msg->args[base_idx] || !msg->args[base_idx+1] || !msg->args[base_idx+2] || !msg->args[base_idx+3] || !msg->args[base_idx+4] || !msg->args[base_idx+5]) {
            return false;
        }
        msg->arg_count += 6;
    }
    return true;
}

Message* create_status_response_message(const Device *devices, int device_count, ResourceManager* resource_manager, ReservationManager* reservation_manager) {
    Message *message = create_message(MSG_STATUS_RESPONSE, NULL);
    if (!message) return NULL;
    if (!fill_status_response_args(message, devices, device_count, resource_manager, reservation_manager)) {
        destroy_message(message);
        return NULL;
    }
    return message;
}

Message* create_login_message(const char* username, const char* password) {
    return create_message_with_two_args(MSG_LOGIN, username, password);
}

Message* create_reservation_message(const char *device_id, const char* duration_str) {
    return create_message_with_two_args(MSG_RESERVE_REQUEST, device_id, duration_str);
}

Message* create_error_message(const char* error_message) {
    return create_message(MSG_ERROR, error_message);
}

const char *get_message_type_string(MessageType type) {
    switch (type) {
        case MSG_LOGIN: return "LOGIN";
        case MSG_LOGOUT: return "LOGOUT";
        case MSG_RESERVE_REQUEST: return "RESERVE_REQUEST";
        case MSG_RESERVE_RESPONSE: return "RESERVE_RESPONSE";
        case MSG_CANCEL_REQUEST: return "CANCEL_REQUEST";
        case MSG_CANCEL_RESPONSE: return "CANCEL_RESPONSE";
        case MSG_STATUS_REQUEST: return "STATUS_REQUEST";
        case MSG_STATUS_RESPONSE: return "STATUS_RESPONSE";
        case MSG_STATUS_UPDATE: return "STATUS_UPDATE";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_PING_RESPONSE: return "PING_RESPONSE";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void destroy_message(Message *msg) {
    if (!msg) return;
    for (int i = 0; i < msg->arg_count; i++) {
        if (msg->args[i]) {
            free(msg->args[i]);
            msg->args[i] = NULL;
        }
    }
    free(msg);
}

static bool read_message_arguments(SSL* ssl, Message* msg, uint32_t arg_count) {
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t arg_len_net;
        if (net_recv(ssl, &arg_len_net, sizeof(arg_len_net)) != sizeof(arg_len_net)) return false;
        uint32_t arg_len = ntohl(arg_len_net);
        if (arg_len >= MAX_ARG_LENGTH) return false;
        msg->args[i] = (char*)malloc(arg_len + 1);
        if (!msg->args[i]) return false;
        if (net_recv(ssl, msg->args[i], arg_len) != (ssize_t)arg_len) {
            free(msg->args[i]);
            msg->args[i] = NULL;
            return false;
        }
        msg->args[i][arg_len] = '\0';
        msg->arg_count++;
    }
    return true;
}

static bool read_message_data(SSL* ssl, Message* msg) {
    uint32_t data_len_net;
    if (net_recv(ssl, &data_len_net, sizeof(data_len_net)) != sizeof(data_len_net)) return false;
    uint32_t data_len = ntohl(data_len_net);
    if (data_len > 0) {
        if (data_len >= MAX_MESSAGE_LENGTH) return false;
        if (net_recv(ssl, msg->data, data_len) != (ssize_t)data_len) return false;
        msg->data[data_len] = '\0';
    }
    return true;
}

Message* receive_message(SSL* ssl) {
    uint32_t type_net, arg_count_net;
    if (net_recv(ssl, &type_net, sizeof(type_net)) != sizeof(type_net)) return NULL;
    if (net_recv(ssl, &arg_count_net, sizeof(arg_count_net)) != sizeof(arg_count_net)) return NULL;
    MessageType type = ntohl(type_net);
    uint32_t arg_count = ntohl(arg_count_net);
    Message* message = create_message(type, NULL);
    if (!message) return NULL;
    if (!read_message_arguments(ssl, message, arg_count)) {
        destroy_message(message);
        return NULL;
    }
    if (!read_message_data(ssl, message)) {
        destroy_message(message);
        return NULL;
    }
    return message;
}

const char *get_device_status_string(DeviceStatus status) {
    switch (status) {
        case DEVICE_AVAILABLE: return "available";
        case DEVICE_RESERVED: return "reserved";
        case DEVICE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}