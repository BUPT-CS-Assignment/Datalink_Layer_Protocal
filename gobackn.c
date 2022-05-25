#include "datalink.h"
#include "protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_SEQ 31
#define DATA_TIMER 11600

typedef unsigned char seq_nr;
typedef struct frame {
    FRAME_KIND kind;
    seq_nr ack;
    seq_nr seq;
    unsigned char data[PKT_LEN];
    unsigned int padding;
} FRAME;

static bool no_nak = true;

static int phl_ready = 0;

static int between(seq_nr a, seq_nr b, seq_nr c) {
    return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}

static void put_frame(seq_nr *frame, int len) {
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(FRAME_KIND fk, seq_nr frame_seq, seq_nr ack_seq, unsigned char out_buf[][PKT_LEN]) {
    FRAME frame;
    frame.kind = fk;                                 //帧类型
    frame.seq = frame_seq;                           //帧序号
    frame.ack = (ack_seq + MAX_SEQ) % (MAX_SEQ + 1); //捎带ACK序号
    if (fk == FRAME_DATA) {
        memcpy(frame.data, out_buf[frame_seq], PKT_LEN);
        dbg_frame("Send DATA %d with ACK %d, ID %d""\n", frame.seq, frame.ack, *(short *)frame.data);
        put_frame((seq_nr *)&frame, 3 + PKT_LEN);
        start_timer(frame_seq, DATA_TIMER); //启动数据定时器
    } else if (fk == FRAME_NAK) {
        no_nak = false;
        dbg_frame(L_BLUE "Send NAK %d" NONE "\n", frame.seq);
        put_frame((unsigned char *)&frame, 2);
        dbg_frame(L_BLUE "Send NAK %d" NONE "\n", frame.seq);
    }
    stop_ack_timer();
}

static void inc(seq_nr *seq) {
    *seq = (*seq + 1) % (MAX_SEQ + 1); //数据指针后移
}

int main(int argc, char **argv) {
    seq_nr out_buf[MAX_SEQ][PKT_LEN];
    seq_nr nbuffered = 0;
    seq_nr ack_expected = 0;
    seq_nr next_frame_to_send = 0;
    seq_nr frame_expected = 0;

    int event, arg;
    FRAME f;
    int len = 0;

    protocol_init(argc, argv);
    lprintf("Reconstruct by YYH, build: " __DATE__ "  "__TIME__
            "\n");

    enable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        /* 网络层就绪 */
        case NETWORK_LAYER_READY:
        /* 从网络层拿包 */
        get_packet(out_buf[next_frame_to_send]);
        ++nbuffered;
        /* 发送数据包 */
        send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);
        inc(&next_frame_to_send);
        break;

        /* 物理层就绪 */
        case PHYSICAL_LAYER_READY:
        phl_ready = 1;
        break;

        /* 收到数据包 */
        case FRAME_RECEIVED:
        len = recv_frame((seq_nr *)&f, sizeof f);
        /* CRC 校验 */
        if (len < 5 || crc32((seq_nr *)&f, len) != 0) {
            dbg_event(L_RED "**** Receiver Error, Bad CRC Checksum" NONE "\n");
            if (no_nak) {
            /* 校验错误且未发送NAK */
                send_data_frame(FRAME_NAK, frame_expected, 0, 0);
            }
            break;
        }
        /* 数据包 */
        if (f.kind == FRAME_DATA) {
            if (f.seq == frame_expected) {
                dbg_frame("Recv DATA %d with piggyback ACK %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
                put_packet(f.data, len - 7);
                no_nak = true;
                inc(&frame_expected);
            } else if (no_nak) {
            /* 序号有误，发送NAK */
                send_data_frame(FRAME_NAK, frame_expected, 0, 0);
            }
        }
        /* NAK包 */
        if (f.kind == FRAME_NAK) {
            dbg_frame(L_PURPLE "Recv NAK %d" NONE "\n", ack_expected);
            next_frame_to_send = ack_expected;
            dbg_frame("nbuffered: %d\n", nbuffered);
            for (seq_nr i = 1; i <= nbuffered; i++) { // 0 or 1?
                dbg_frame("REsend DATA %d with ACK %d\n", next_frame_to_send, (frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
                send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);
                inc(&next_frame_to_send);
            }
            break;
        }
        /* 处理捎带ACK */
        while (between(ack_expected, f.ack, next_frame_to_send)) {
            --nbuffered;
            stop_timer(ack_expected);
            inc(&ack_expected);
        }
        break;

        /* 数据定时器超时 */
        case DATA_TIMEOUT:
        next_frame_to_send = ack_expected;
        dbg_event(YELLOW "---- DATA %d timeout" NONE "\n", arg);
        dbg_frame("nbuffered: %d\n", nbuffered);
        for (seq_nr i = 1; i <= nbuffered; i++) { // 0 or 1?
            send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected,out_buf);
            inc(&next_frame_to_send);
        }
        }

        if (nbuffered < MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}