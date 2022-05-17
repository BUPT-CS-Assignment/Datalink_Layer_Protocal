#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"


static u_char out_buf[NR_BUFS][PKT_LEN];
static u_char in_buf[NR_BUFS][PKT_LEN];
static u_char nbuffered = 0;
static u_char ack_expected = 0;
static u_char next_frame_to_send = 0;

static u_char frame_expected = 0;
static u_char too_far = NR_BUFS;
static boolean arrived[NR_BUFS];

static boolean no_nak = true;

static int phl_ready = 0;

static int between(u_char a, u_char b, u_char c){
    return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}

static void put_frame(u_char* frame, int len){
    *(unsigned int*)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(FRAME_KIND fk, u_char frame_seq,u_char ack_seq){
    FRAME frame;
    frame.kind = fk;        //帧类型
    frame.seq = frame_seq;  //帧序号
    frame.ack = ack_seq;    //捎带ACK序号
    if(fk == DATA){
        memcpy(frame.data, out_buf[frame_seq % NR_BUFS], PKT_LEN);
        dbg_frame("Send DATA %d with ACK %d, ID %d""\n", frame.seq, frame.ack, *(short*)frame.data);
        put_frame((u_char*)&frame, 3 + PKT_LEN);
        start_timer(frame_seq, DATA_TIMER);     //启动数据定时器
    }
    else if(fk == ACK){
        dbg_frame(L_BLUE"Send ACK %d"NONE"\n", frame.seq);
        put_frame((u_char*)&frame, 2);
    }
    else if(fk == NAK){
        no_nak = false;
        dbg_frame(L_BLUE"Send NAK %d"NONE"\n", frame.seq);
        put_frame((u_char*)&frame, 2);
    }
    stop_ack_timer();
}

static void inc(u_char* seq){
    *seq = (*seq + 1) % (MAX_SEQ + 1);      //数据指针后移
}


int main(int argc, char **argv)
{
	int event, arg;
	FRAME f;
	int len = 0;
    memset(arrived,0,sizeof(arrived));

	protocol_init(argc, argv);
	lprintf("Designed by Jianxff, build: " __DATE__ "  "__TIME__"\n");

	enable_network_layer();

    for(;;){
        event = wait_for_event(&arg);

        switch(event){
            /* 网络层就绪 */
            case NETWORK_LAYER_READY:
                /* 从网络层拿包 */
                get_packet(out_buf[next_frame_to_send % NR_BUFS]);
                ++nbuffered;
                /* 发送数据包 */
                send_data_frame(DATA,next_frame_to_send,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                inc(&next_frame_to_send);
                break;

            /* 物理层就绪 */
            case PHYSICAL_LAYER_READY:
                phl_ready = 1;
                break;

            /* 收到数据包 */
            case FRAME_RECEIVED:
                len = recv_frame((u_char *)&f, sizeof f);
                /* CRC 校验 */
			    if (len < 5 || crc32((u_char *)&f, len) != 0){
				    dbg_event(L_RED"**** Receiver Error, Bad CRC Checksum"NONE"\n");
                    if(no_nak){
                        /* 校验错误且未发送NAK */
                        send_data_frame(NAK,frame_expected,0);
                    }
                    break;
			    }
                /* 数据包 */
                if(f.kind == DATA){
                    dbg_frame(WHITE"Recv DATA %d with "L_GREEN"ACK %d"WHITE", ID %d"NONE"\n", f.seq, f.ack, *(short *)f.data);
                    if(f.seq != frame_expected && no_nak){
                        /* 不是需要的第一个帧 */
                        send_data_frame(NAK,frame_expected,0);
                    }else{
                        /* 启动ACK定时器 */
                        start_ack_timer(ACK_TIMER);
                    }
                    /* 帧正确 */
                    if(between(frame_expected,f.seq,too_far) && !arrived[f.seq % NR_BUFS]){
                        arrived[f.seq % NR_BUFS] = true;
                        memcpy(in_buf[f.seq % NR_BUFS],f.data,len - 7);
                        /* 按序交付网络层 */
                        while(arrived[frame_expected % NR_BUFS]){
                            put_packet(in_buf[frame_expected % NR_BUFS],len - 7);
                            no_nak = true;
                            arrived[frame_expected % NR_BUFS] = false;
                            inc(&frame_expected);
                            inc(&too_far);
                            start_ack_timer(ACK_TIMER);
                        }
                    }
                }
                /* NAK包 */
                if(f.kind == NAK){
                    dbg_frame(L_PURPLE"Recv NAK %d"NONE"\n", f.seq);
                    if(between(ack_expected,f.seq,next_frame_to_send)){
                        /* 发送相应数据包 */
                        send_data_frame(DATA,f.seq,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                    }
                    break;
                }
                /* ACK包 */
                if(f.kind == ACK){
                    dbg_frame(L_GREEN"Recv ACK %d"NONE"\n", f.seq);
                    f.ack = f.seq;
                }
                /* 处理ACK包或捎带ACK */
                while(between(ack_expected,f.ack,next_frame_to_send)){
                    --nbuffered;
                    stop_timer(ack_expected);
                    inc(&ack_expected);
                }
                break;

            /* 数据定时器超时 */
            case DATA_TIMEOUT:
                dbg_event(YELLOW"---- DATA %d timeout"NONE"\n", arg);
                /* 重发相应的包 */
                send_data_frame(DATA,arg,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                break;

            /* ACK超时 */
            case ACK_TIMEOUT:
                dbg_event(YELLOW"---- ACK timeout"NONE"\n");
                /* 发送单独ACK包 */
                send_data_frame(ACK,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1),0);
                break;
        }

        if (nbuffered < NR_BUFS && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
    }
}