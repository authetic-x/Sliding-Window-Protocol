#include "util.h"

//Linked list functions
int ll_get_length(LLnode * head)
{
    LLnode * tmp;
    int count = 1;
    if (head == NULL)
        return 0;
    else
    {
        tmp = head->next;
        while (tmp != head)
        {
            count++;
            tmp = tmp->next;
        }
        return count;
    }
}

void ll_append_node(LLnode ** head_ptr, 
                    void * value)
{
    LLnode * prev_last_node;
    LLnode * new_node;
    LLnode * head;

    if (head_ptr == NULL)
    {
        return;
    }
    
    //Init the value pntr
    head = (*head_ptr);
    new_node = (LLnode *) malloc(sizeof(LLnode));
    new_node->value = value;

    //The list is empty, no node is currently present
    if (head == NULL)
    {
        (*head_ptr) = new_node;
        new_node->prev = new_node;
        new_node->next = new_node;
    }
    else
    {
        //Node exists by itself
        prev_last_node = head->prev;
        head->prev = new_node;
        prev_last_node->next = new_node;
        new_node->next = head;
        new_node->prev = prev_last_node;
    }
}


LLnode * ll_pop_node(LLnode ** head_ptr)
{
    LLnode * last_node;
    LLnode * new_head;
    LLnode * prev_head;

    prev_head = (*head_ptr);
    if (prev_head == NULL)
    {
        return NULL;
    }
    last_node = prev_head->prev;
    new_head = prev_head->next;

    //We are about to set the head ptr to nothing because there is only one thing in list
    if (last_node == prev_head)
    {
        (*head_ptr) = NULL;
        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    }
    else
    {
        (*head_ptr) = new_head;
        last_node->next = new_head;
        new_head->prev = last_node;

        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    }
}

void ll_destroy_node(LLnode * node)
{
    if (node->type == llt_string)
    {
        free((char *) node->value);
    }
    free(node);
}

//Compute the difference in usec for two timeval objects
long timeval_usecdiff(struct timeval *start_time, 
                      struct timeval *finish_time)
{
  long usec;
  usec=(finish_time->tv_sec - start_time->tv_sec)*1000000;
  usec+=(finish_time->tv_usec- start_time->tv_usec);
  return usec;
}


//Print out messages entered by the user
void print_cmd(Cmd * cmd)
{
    fprintf(stderr, "src=%d, dst=%d, message=%s\n", 
           cmd->src_id,
           cmd->dst_id,
           cmd->message);
}


char * convert_frame_to_char(Frame * frame)
{
  //TODO: You should implement this as necessary
  char * char_buffer = (char *) malloc(MAX_FRAME_SIZE);
  memset(char_buffer,
          0,
          MAX_FRAME_SIZE);
  memcpy(char_buffer, 
          frame,
          MAX_FRAME_SIZE);
  return char_buffer;
}


Frame * convert_char_to_frame(char * char_buf)
{
    //TODO: You should implement this as necessary
    Frame * frame = (Frame *) malloc(sizeof(Frame));
    memset(frame,
           0,
           MAX_FRAME_SIZE);
    memcpy(frame, 
           char_buf,
           MAX_FRAME_SIZE);
    return frame;
}

// 判断序列号是否在指定区间的辅助函数
int judegeArea(char target, char L, char R) {
  return (
    (L < R && L <= target && target <= R) ||
    (L > R && (target >= L || target <= R))
  );
}

// 设置超时时间
void setTimeout(struct timeval * timeout) {
  gettimeofday(timeout, NULL);
  timeout->tv_usec += 100000;
  if (timeout->tv_usec >= 1000000) {
    timeout->tv_usec -= 1000000;
    timeout->tv_sec += 1;
  }
}

//crc
char get_bit (char byte, int pos) {
  char temp = byte >> pos;
  if ((temp & 0x01) == 0)
    return 0;
  else
    return 1;
}

char crc8(char * array, int array_len) {
  char poly = 0x07;
  char crc = array[0];
  int i, j;
  for (i = 1; i < array_len; i++) {
    char next_byte = array[i];
    for (j = 7; j >= 0; j--) {
      if ((crc & 0x80) == 0) {
        crc = crc << 1;
        crc = crc | get_bit(next_byte, j);
      }
      else {
        crc = crc << 1;
        crc = crc | get_bit(next_byte, j);
        crc = crc ^ poly;
      }
    }
  }
  return crc;
}

void append_crc(char * array, int array_len) {
  array[array_len-1] = 0x00;
  char crc = crc8(array, array_len);
  array[array_len-1] = array[array_len-1] ^ crc;
}

int is_corrupted(char * array, int array_len) {
  char crc = crc8(array, array_len);
  if (crc == 0)
    return 1;
  else
    return 0;
}

void ll_split_head(LLnode ** head_ptr, int payload_size) {
    if (head_ptr == NULL || *head_ptr == NULL) {
        return;
    }
    LLnode* head = *head_ptr;
    Cmd* head_cmd = (Cmd*) head->value;
    char * msg = head_cmd -> message;
    if (strlen(msg) < payload_size) {
        return;
    }
    int i;
    Cmd * next_cmd;
    for (i = payload_size; i < strlen(msg); i += payload_size) {
        //next = (LLnode*) malloc(sizeof (LLnode));
        next_cmd = (Cmd *) malloc(sizeof(Cmd));
        char * cmd_msg = (char *) malloc((payload_size + 1) * sizeof(char));
        memset(cmd_msg, 0, (payload_size + 1) * sizeof(char));
        strncpy(cmd_msg, msg + i, payload_size);
        //fprintf(stderr, "MSG = %s\n", cmd_msg);
        next_cmd->src_id = head_cmd->src_id;
        next_cmd->dst_id = head_cmd->dst_id;
        next_cmd->message = cmd_msg;
        ll_append_node(head_ptr, next_cmd);
    }
    msg[payload_size] = '\0';
}
