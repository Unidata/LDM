/*
 * Module implementation of Circuit-TCP
 */     
        
//#include <linux/config.h>
#include <linux/module.h>
#include <net/tcp.h>
        
static int initial = 500;
static int bw = 1000;
static int scale = 120;
int instance = 0;
int assigned = 0;
int percent = 0;                
int lowestrtt = 150000;
        
module_param(initial, int, 0644);
MODULE_PARM_DESC(initial, "Initial cwnd in packets");
module_param(bw, int, 0644);
MODULE_PARM_DESC(bw, "Circuit/Virtual-circuit bandwidth in Mbps");
module_param(scale, int, 0644);
MODULE_PARM_DESC(scale, "Percent to scale cwnd (100 = BDP), default 120");

                
static void ctcp_init(struct sock *sk)
{
        struct tcp_sock *tp = tcp_sk(sk);
        tp->snd_cwnd = initial;
        tp->snd_ssthresh = initial-1;
        tp->rcv_ssthresh = sysctl_tcp_rmem[1];
        instance = initial;
        assigned = bw;
        percent = scale;
}

/* Function called after receipt of an ACK */
static void ctcp_cong_avoid(struct sock *sk, u32 ack,
                                u32 seq_rtt, u32 in_flight, int data_acked)
{
        //if(tcp_sk(sk)->srtt==0){
        //      tcp_sk(sk)->snd_cwnd = instance;
        //      return;
        //}
 
        if(tcp_sk(sk)->srtt < lowestrtt){
                lowestrtt = tcp_sk(sk)->srtt;

                //if(lowestrtt<1000)
                //        lowestrtt = 1000;

                instance = (assigned*lowestrtt*100)/11680;
                instance = (instance*scale)/100;
        }

        tcp_sk(sk)->snd_cwnd = instance;
        //tcp_sk(sk)->snd_ssthresh = tcp_sk(sk)->rcv_rtt_est.rtt;
}

/* Function called after a loss */
static u32 ctcp_ssthresh(struct sock *sk)
{
        return instance;
}

/* Function called after a loss, but also after the above ctcp_ssthresh
   was called */
static u32 ctcp_min_cwnd(struct sock *sk)
{
        return instance;
}

static void ctcp_set_state(struct sock *sk, u8 new_state)
{
        inet_csk(sk)->icsk_ca_state = TCP_CA_Open;
}

static u32 ctcp_undo_cwnd(struct sock *sk)
{
        return instance;
}

static void ctcp_cwnd_event(struct sock *sk)
{
        tcp_sk(sk)->snd_cwnd = instance;
}

static struct tcp_congestion_ops tcp_ctcp = {
        .init           = ctcp_init,
        .ssthresh       = ctcp_ssthresh,
        .cong_avoid     = ctcp_cong_avoid,
        .min_cwnd       = ctcp_min_cwnd,
        //.set_state      = ctcp_set_state,
        .undo_cwnd      = ctcp_undo_cwnd,
        .cwnd_event     = ctcp_cwnd_event,
        .owner  = THIS_MODULE,
        .name           = "ctcp",
};

static int __init ctcp_register(void)
{
        return tcp_register_congestion_control(&tcp_ctcp);
}

static void __exit ctcp_unregister(void)
{
        tcp_unregister_congestion_control(&tcp_ctcp);
}

module_init(ctcp_register);
module_exit(ctcp_unregister);

MODULE_AUTHOR("Mark McGinley");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Circuit-TCP");
