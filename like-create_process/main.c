#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/// Linux process 和 thread 之间共享与隔离关系的测试代码
/// 
/// 

/*

process: 进程间独立地址空间，内存隔离

*/
void memory_aspect_process() {
  int data = 100;
  pid_t pid = fork();
  if (pid == 0) {
    // child process
    data += 50;
    printf("child: data = %d\n", data);
  } else {
    // parent process
    data += 49;
    printf("parent: data = %d\n", data);
  }
}

/*

thread: 共享全局内存、静态内存、堆内存，独立栈内存(复制了一份历史栈内存)

*/
int sharing_data = 100;
static int static_data = 100;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    int *heap_ptr;
    int *stack_ptr;
} Ptr; 
void *fn_memory_thread(void *arg) {
  Ptr *ptr = (Ptr *)arg;
  static int inner_static_data = 100;
  pid_t pid = getpid();
  pthread_t ptid = pthread_self();
  pthread_mutex_lock(&mutex);
  sharing_data += 1; // data race
  static_data += 1;  // data race
  inner_static_data += 1;
  *(ptr->stack_ptr+1) += 1;
  pthread_mutex_unlock(&mutex);
  printf(" child: pid: %d, tid: %lu, sharing_data = %d, static_data = %d, heap_pointer/heap_value "
         "= 0x%lx/%d, stack_pointer/stack_value = 0x%lx/%d, inner_static_data = %d\n",
         pid, (unsigned long)ptid, sharing_data, static_data, (unsigned long)ptr->heap_ptr, *ptr->heap_ptr, (unsigned long)ptr->stack_ptr, *ptr->stack_ptr, inner_static_data);
  return NULL;
}
void memory_aspect_thread() {
  pthread_t ptid0, ptid1;
  int *heap_ptr = (int *)malloc(sizeof(int));
  *heap_ptr = 0xAE86;
  int stack_val[2] = {0xAE88, 99};
  int next_stack_val = 1234;
  Ptr ptr = {heap_ptr, stack_val};
  pthread_create(&ptid0, NULL, fn_memory_thread, &ptr);
  pthread_create(&ptid1, NULL, fn_memory_thread, &ptr);
  pthread_join(ptid0, NULL);
  pthread_join(ptid1, NULL);
  printf("parent: pid: %d, tid: %lu, sharing_data = %d, static_data = %d, heap_pointer/heap_value = 0x%lx/%d, stack_pointer/stack_value = 0x%lx/%d, %d\n",
         getpid(),
         (unsigned long)pthread_self(),
         sharing_data, static_data, (unsigned long)heap_ptr, *heap_ptr, (unsigned long)stack_val, stack_val[0], stack_val[1]);
  free(heap_ptr);
}

/*

process: IPC-管道通信示例

*/
void communication_aspect_process() {
    char msg2parent[] = "hello parent!";
    char msg2child[] = "hello child!";

    int fd[2];
    pipe(fd);

    if ( fork() == 0) {
        // child process
        write(fd[1], msg2parent, sizeof(msg2parent));
        sleep(1);

        char buf[1024];
        read(fd[0], buf, sizeof(msg2child));
        printf(" child received: %s\n", buf);

        close(fd[0]);
        close(fd[1]);
    }else {
        // parent process
        char buf[1024];
        read(fd[0], buf, sizeof(msg2parent));
        printf("parent received: %s\n", buf);

        write(fd[1], msg2child, sizeof(msg2child));

        close(fd[0]);
        close(fd[1]);
        wait(NULL);
    }
}

/*

how linux create a new process with given image

*/
int create_process(int argc, char **argv) {
  printf("argc: %d\n", argc);
  for (int i = 0; i < argc; i++) {
    printf("argv[%d]: %s\n", i, argv[i]);
  }
  int status = 0;

  pid_t pid = fork();
  if (pid == 0) {
    execv(argv[1], &argv[2]);
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    status = -1;
  } else {
    if (waitpid(pid, &status, 0) != pid) {
      status = -1;
    }
  }
  return status;
}

int main() {
  // memory_aspect_process();
  memory_aspect_thread();
  // communication_aspect_process();
}