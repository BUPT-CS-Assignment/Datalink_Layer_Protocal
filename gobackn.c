#include "datalink.h"
#include "protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define DATA_TIMER 1200
#define MAX_SEQ 10

typedef unsigned char seq_nr;
typedef unsigned char FRAME_KIND;
typedef struct {
  FRAME_KIND kind;
  seq_nr ack;
  seq_nr seq;
  unsigned char data[PKT_LEN];
  unsigned int padding;
} FRAME;

static int phl_ready = 0;

static bool between(seq_nr a, seq_nr b, seq_nr c) {
  return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}
void inc(seq_nr *seq) { *seq = (*seq < MAX_SEQ) ? (*seq + 1) : 0; }

static void put_frame(unsigned char *frame, int len) {
  *(unsigned int *)(frame + len) = crc32(frame, len);
  send_frame(frame, len + 4);
  phl_ready = 0;
}
static void send_data(FRAME_KIND fk, seq_nr frame_nr, seq_nr frame_expected,
                      unsigned char buffer[][PKT_LEN]) {
  FRAME s;
  s.kind = fk;
  memcpy(s.data, buffer[frame_nr], PKT_LEN);
  s.seq = frame_nr;
  s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

  dbg_frame("Send DATA %d with piggyback ACK %d, ID %d"
            "\n",
            s.seq, s.ack, *(short *)s.data);
  put_frame((unsigned char *)&s, 3 + PKT_LEN);

  start_timer(frame_nr, DATA_TIMER);
}

int main(int argc, char **argv) {
  seq_nr next_frame_to_send = 0;
  seq_nr ack_expected = 0;
  seq_nr frame_expected = 0;
  FRAME f;
  unsigned char buffer[MAX_SEQ + 1][PKT_LEN] = {0};
  seq_nr nbuffered = 0;
  int event, arg, len = 0;
  protocol_init(argc, argv);
  lprintf("Reconstructed by YYH, build: " __DATE__ "  "__TIME__
          "\n");
  enable_network_layer();
  while (1) {
    event = wait_for_event(&arg);
    switch (event) {
    case NETWORK_LAYER_READY:
      get_packet(buffer[next_frame_to_send]);
      nbuffered++;
      // dbg_frame("nbuffered: %d\n",nbuffered);
      send_data(FRAME_DATA, next_frame_to_send, frame_expected,
                buffer); //
      inc(&next_frame_to_send);
      break;
    case PHYSICAL_LAYER_READY:
      phl_ready = 1;
      break;
    case FRAME_RECEIVED:
      len = recv_frame((unsigned char *)&f, sizeof(f));
      if (len < 5 || crc32((unsigned char *)&f, sizeof(f))) {
        dbg_event("**** Receiver Error, Bad CRC Checksum\n");
        break;
      }
      // dbg_frame("Got unconfrimed DATA %d with piggyback ACK %d,ID %d,
      // expecting seq %d\n ",
      //           f.seq, f.ack, *(short *)f.data, frame_expected);
      if (f.seq == frame_expected) {
        dbg_frame("Recv DATA %d with piggyback ACK %d, ID %d\n", f.seq, f.ack,
                  *(short *)f.data);
        put_packet(f.data, len - 7);
        inc(&frame_expected);
      }
      while (between(ack_expected, f.ack, next_frame_to_send)) {
        nbuffered--;
        // dbg_frame("nbuffered: %d\n", nbuffered);
        stop_timer(ack_expected);
        inc(&ack_expected);
      }
      break;
    case DATA_TIMEOUT:
      next_frame_to_send = ack_expected;
      dbg_event("---- DATA %d timeout\n", ack_expected);
      //   dbg_frame("nbuffered: %d\n", nbuffered);
      for (seq_nr i = 1; i <= nbuffered; i++) { // 0 or 1?
        send_data(FRAME_DATA, next_frame_to_send, frame_expected, buffer);
        inc(&next_frame_to_send);
      }
      // dbg_frame("nbuffered: %d\n", nbuffered);
    }
    if (nbuffered < MAX_SEQ) {
      enable_network_layer();
    } else {
      disable_network_layer();
    }
  }
}