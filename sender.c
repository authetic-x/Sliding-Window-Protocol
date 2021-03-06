#include "sender.h"

void init_sender(Sender * sender, int id)
{
  //TODO: You should fill in this function as necessary
  sender->send_id = id;
  sender->input_cmdlist_head = NULL;
  sender->input_framelist_head = NULL;

  sender->seqNum = 0;
  sender->LMargin = 0;
  sender->windowSize = 0;
  for (int i = 0; i < SWS; i ++ ) {
    sender->sendWindow[i].full = 0;
  }
}

struct timeval * sender_get_next_expiring_timeval(Sender * sender)
{
    //TODO: You should fill in this function so that it returns the next timeout that should occur
    return NULL;
}


void handle_incoming_acks(Sender * sender,
                          LLnode ** outgoing_frames_head_ptr)
{
  int incoming_ack_length = ll_get_length(sender->input_framelist_head);
  while(incoming_ack_length > 0) {
    LLnode * ll_ack_node = ll_pop_node(&sender->input_framelist_head);
    incoming_ack_length = ll_get_length(sender->input_framelist_head);
    char * raw_char_buff = (char*)ll_ack_node->value;
    if (!is_corrupted(raw_char_buff, MAX_FRAME_SIZE)) continue;
    Frame * ackFrame = convert_char_to_frame(raw_char_buff);
    free(raw_char_buff);
    free(ll_ack_node);
    if (sender->send_id == ackFrame->src_id) {
      if (!judegeArea(ackFrame->seqNum, sender->LMargin, sender->seqNum)) break;

      fprintf(stderr, "ack number: %d\n", ackFrame->seqNum);
      while (ackFrame->seqNum != sender->LMargin) {
        fprintf(stderr, "[while]ack_src_id: %d\n", ackFrame->src_id);
        for (int i = 0; i < SWS; i ++ ) {
          if (sender->sendWindow[i].full) {
            Frame * frame = (Frame*)sender->sendWindow[i].msg;
            if (frame->seqNum == sender->LMargin) {
              sender->sendWindow[i].full = 0;
              sender->windowSize--;
              break;
            }
          }
        }
        sender->LMargin++;
      }
    }
    free(ackFrame);
  }
}


void handle_input_cmds(Sender * sender,
                       LLnode ** outgoing_frames_head_ptr)
{
  int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
  //ll_split_head(&sender->input_cmdlist_head, FRAME_PAYLOAD_SIZE - 1);
  while (input_cmd_length > 0 && sender->windowSize < SWS)
  {
    unsigned char RMargin = sender->LMargin + SWS - 1;
    if (!judegeArea(sender->seqNum, sender->LMargin, RMargin))
      break;

    LLnode * ll_input_cmd_node = ll_pop_node(&sender->input_cmdlist_head);
    input_cmd_length = ll_get_length(sender->input_cmdlist_head);
    Cmd * outgoing_cmd = (Cmd *) ll_input_cmd_node->value;
    free(ll_input_cmd_node);

    int msg_length = strlen(outgoing_cmd->message);
    if (msg_length > MAX_FRAME_SIZE)
    {
      //TODO: Do something about messages that exceed the frame size
      printf("<SEND_%d>: sending messages of length greater than %d is not implemented\n", sender->send_id, MAX_FRAME_SIZE);
    }
    else
    {
      Frame * outgoing_frame = (Frame *) malloc (sizeof(Frame));
      strcpy(outgoing_frame->data, outgoing_cmd->message);
      outgoing_frame->src_id = outgoing_cmd->src_id;
      outgoing_frame->dst_id = outgoing_cmd->dst_id;
      outgoing_frame->seqNum = sender->seqNum;
      sender->seqNum++;
      free(outgoing_cmd->message);
      free(outgoing_cmd);

      char * outgoing_charbuf = convert_frame_to_char(outgoing_frame);
      append_crc(outgoing_charbuf, MAX_FRAME_SIZE);
      ll_append_node(outgoing_frames_head_ptr,
                      outgoing_charbuf);
      for (int i = 0; i < SWS; i ++ ) {
        if (sender->sendWindow[i].full == 0) {
          sender->sendWindow[i].full = 1;
          sender->windowSize++;
          struct timeval * timeout = malloc(sizeof(struct timeval));
          setTimeout(timeout);
          sender->sendWindow[i].timeout = timeout;
          sender->sendWindow[i].msg = (Frame*)malloc(MAX_FRAME_SIZE);
          memcpy(sender->sendWindow[i].msg, outgoing_frame, MAX_FRAME_SIZE);
          break;
        }
      }
      free(outgoing_frame);
    }
  }   
}


void handle_timedout_frames(Sender * sender,
                            LLnode ** outgoing_frames_head_ptr)
{
  struct timeval * currentTime = malloc(sizeof(struct timeval));
  gettimeofday(currentTime, NULL);
  for (int i = 0; i < SWS; i++ ) {
    if (sender->sendWindow[i].full) {
      long timeDiff = timeval_usecdiff(currentTime, sender->sendWindow[i].timeout);
      if (timeDiff < 0) {
        Frame * outgoing_frame = (Frame*)sender->sendWindow[i].msg;
        char * buffer = convert_frame_to_char(outgoing_frame);
        append_crc(buffer, MAX_FRAME_SIZE);
        ll_append_node(outgoing_frames_head_ptr, buffer);
        setTimeout(sender->sendWindow[i].timeout);
      }
    }
  }
}


void * run_sender(void * input_sender)
{    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Sender * sender = (Sender *) input_sender;    
    LLnode * outgoing_frames_head;
    struct timeval * expiring_timeval;
    long sleep_usec_time, sleep_sec_time;
    
    //This incomplete sender thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up
    //2. Grab the mutex protecting the input_cmd/inframe queues
    //3. Dequeues messages from the input queue and adds them to the outgoing_frames list
    //4. Releases the lock
    //5. Sends out the messages

    pthread_cond_init(&sender->buffer_cv, NULL);
    pthread_mutex_init(&sender->buffer_mutex, NULL);

    while(1)
    {    
        outgoing_frames_head = NULL;

        //Get the current time
        gettimeofday(&curr_timeval, 
                     NULL);

        //time_spec is a data structure used to specify when the thread should wake up
        //The time is specified as an ABSOLUTE (meaning, conceptually, you specify 9/23/2010 @ 1pm, wakeup)
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;

        //Check for the next event we should handle
        expiring_timeval = sender_get_next_expiring_timeval(sender);

        //Perform full on timeout
        if (expiring_timeval == NULL)
        {
            time_spec.tv_sec += WAIT_SEC_TIME;
            time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        }
        else
        {
            //Take the difference between the next event and the current time
            sleep_usec_time = timeval_usecdiff(&curr_timeval,
                                               expiring_timeval);

            //Sleep if the difference is positive
            if (sleep_usec_time > 0)
            {
                sleep_sec_time = sleep_usec_time/1000000;
                sleep_usec_time = sleep_usec_time % 1000000;   
                time_spec.tv_sec += sleep_sec_time;
                time_spec.tv_nsec += sleep_usec_time*1000;
            }   
        }

        //Check to make sure we didn't "overflow" the nanosecond field
        if (time_spec.tv_nsec >= 1000000000)
        {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        
        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames or input commands should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&sender->buffer_mutex);

        //Check whether anything has arrived
        int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(sender->input_framelist_head);
        
        //Nothing (cmd nor incoming frame) has arrived, so do a timed wait on the sender's condition variable (releases lock)
        //A signal on the condition variable will wakeup the thread and reaquire the lock
        if (input_cmd_length == 0 &&
            inframe_queue_length == 0)
        {
            
            pthread_cond_timedwait(&sender->buffer_cv, 
                                   &sender->buffer_mutex,
                                   &time_spec);
        }
        //Implement this
        handle_incoming_acks(sender,
                             &outgoing_frames_head);

        //Implement this
        handle_input_cmds(sender,
                          &outgoing_frames_head);

        pthread_mutex_unlock(&sender->buffer_mutex);


        //Implement this
        handle_timedout_frames(sender,
                               &outgoing_frames_head);

        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        
        while(ll_outgoing_frame_length > 0)
        {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *)  ll_outframe_node->value;

            //Don't worry about freeing the char_buf, the following function does that
            send_msg_to_receivers(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);
    return 0;
}
