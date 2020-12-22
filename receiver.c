#include "receiver.h"

void init_receiver(Receiver * receiver,
                   int id)
{
    receiver->recv_id = id;
    receiver->input_framelist_head = NULL;

    // 初始化窗口相关数据
    receiver->windowSize = 0;
    receiver->LMargin = 0;
    for (int i = 0; i < RWS; i ++ ) {
      receiver->receiverWindow[i].full = 0;
      receiver->receiverWindow[i].seqNum = 0;
    }
}


void handle_incoming_msgs(Receiver * receiver,
                          LLnode ** outgoing_frames_head_ptr)
{
  int incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
  while (incoming_msgs_length > 0)
  {
    //Pop a node off the front of the link list and update the count
    LLnode * ll_inmsg_node = ll_pop_node(&receiver->input_framelist_head);
    incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
      
    char * raw_char_buf = (char *) ll_inmsg_node->value;
    if (!is_corrupted(raw_char_buf, MAX_FRAME_SIZE)) continue;
    Frame * inframe = convert_char_to_frame(raw_char_buf);

    if (receiver->recv_id == inframe->dst_id) {
      int LMargin = receiver->LMargin;
      int seqNum = inframe->seqNum;
      unsigned char RMargin = LMargin + RWS - 1;
      
      if (LMargin == seqNum) {
        printf("<RECV_%d>:[%s]\n", receiver->recv_id, inframe->data);

        inframe->seqNum++;
        char *buffer = convert_frame_to_char(inframe);
        append_crc(buffer, MAX_FRAME_SIZE);
        ll_append_node(outgoing_frames_head_ptr, buffer);
        receiver->LMargin++;
        for (int i = 0; i < RWS; i ++ ) {
          if (receiver->receiverWindow[i].full == 1 && 
              receiver->receiverWindow[i].seqNum == receiver->LMargin) {
                receiver->LMargin++;
                receiver->receiverWindow[i].full = 0;
                receiver->windowSize--;
                Frame *frame = (Frame*)receiver->receiverWindow[i].msg;
                //frame->seqNum++;
                printf("<RECV_%d>:[%s]\n", receiver->recv_id, frame->data);
                // ack
                char *buffer = convert_frame_to_char(frame);
                append_crc(buffer, MAX_FRAME_SIZE);
                ll_append_node(outgoing_frames_head_ptr, buffer);
              }
        }
      } else if (judegeArea(seqNum, LMargin, RMargin)) {
          int isReplicated = 0;
          // 判断包是否已缓存
          for (int i = 0; i < RWS; i ++ ) {
            if (receiver->receiverWindow[i].full && receiver->receiverWindow[i].seqNum == seqNum) {
              isReplicated = 1;
              break;
            }
          }
          if (!isReplicated) {
            for (int i = 0; i < RWS; i ++ ) {
              if (receiver->receiverWindow[i].full == 0) {
                receiver->receiverWindow[i].full = 1;
                receiver->windowSize++;
                receiver->receiverWindow[i].seqNum = seqNum;
                receiver->receiverWindow[i].msg = (Frame*)malloc(MAX_FRAME_SIZE);
                memcpy(receiver->receiverWindow[i].msg, inframe, MAX_FRAME_SIZE);
                break;
              }
            }
          }
      } else {
        // 不在区间内直接传回确认
        inframe->seqNum = receiver->LMargin;
        char *buffer = convert_frame_to_char(inframe);
        append_crc(buffer, MAX_FRAME_SIZE);
        ll_append_node(outgoing_frames_head_ptr, buffer);
      }
    }

    free(raw_char_buf);
    free(inframe);
    free(ll_inmsg_node);
  }
}

void * run_receiver(void * input_receiver)
{    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Receiver * receiver = (Receiver *) input_receiver;
    LLnode * outgoing_frames_head;


    //This incomplete receiver thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up if there is nothing in the incoming queue(s)
    //2. Grab the mutex protecting the input_msg queue
    //3. Dequeues messages from the input_msg queue and prints them
    //4. Releases the lock
    //5. Sends out any outgoing messages

    pthread_cond_init(&receiver->buffer_cv, NULL);
    pthread_mutex_init(&receiver->buffer_mutex, NULL);

    while(1)
    {    
        //NOTE: Add outgoing messages to the outgoing_frames_head pointer
        outgoing_frames_head = NULL;
        gettimeofday(&curr_timeval, 
                     NULL);

        //Either timeout or get woken up because you've received a datagram
        //NOTE: You don't really need to do anything here, but it might be useful for debugging purposes to have the receivers periodically wakeup and print info
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;
        time_spec.tv_sec += WAIT_SEC_TIME;
        time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        if (time_spec.tv_nsec >= 1000000000)
        {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&receiver->buffer_mutex);

        //Check whether anything arrived
        int incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
        if (incoming_msgs_length == 0)
        {
            //Nothing has arrived, do a timed wait on the condition variable (which releases the mutex). Again, you don't really need to do the timed wait.
            //A signal on the condition variable will wake up the thread and reacquire the lock
            pthread_cond_timedwait(&receiver->buffer_cv, 
                                   &receiver->buffer_mutex,
                                   &time_spec);
        }

        handle_incoming_msgs(receiver,
                             &outgoing_frames_head);

        pthread_mutex_unlock(&receiver->buffer_mutex);
        
        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames user has appended to the outgoing_frames list
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        while(ll_outgoing_frame_length > 0)
        {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *) ll_outframe_node->value;
            
            //The following function frees the memory for the char_buf object
            send_msg_to_senders(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);

}
