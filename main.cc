/***********************************************
        File Name: main.cc
        Author: Abby Cin
        Mail: abbytsing@gmail.com
        Created Time: 5/9/19 8:23 PM
***********************************************/

#ifndef __linux__
#error "unsupport platform"
#endif

#include <cstdlib>
#include <list>
#include <cstring>
#include <csignal>
#include <functional>
#include <iostream>

extern "C"
{
  // @stack: actually the rbp pointer(at beginning, rbp equal to rsp)
  // @ctx_func: this function is a wrapper for user defined `coroutine` function(or context)
  // the purpose of this function is to know when user `coroutine` is finished
  void* init_stack(void* stack, void* ctx_func);

  // @prev: actually this argument is store rsp of current routine which maybe restore in next context switch
  // @next: it's the routine about executing
  // @self: is the Context object of current running routine
  void switch_stack(void** prev, void* next, void* self = nullptr);
}

template<typename T>
void ctx_function(T* obj);

void yield();

class Context
{
public:
  enum Status
  {
    SLEEP = 0,
    RUNNING,
    STOPPED
  };

  template<typename F, typename... Args>
  void init(F&& f, Args&&... args)
  {
    bp = malloc(STACK_SIZE);
    ::memset(bp, 0, STACK_SIZE);
    sp = init_stack((char*)bp + STACK_SIZE - RED_ZONE, (void*)ctx_function<Context>);
    printf("bp: %p, sp: %p\n", bp, sp);
    fn_ = [fn = std::forward<F>(f), params = std::make_tuple(std::forward<Args>(args)...)] { std::apply(fn, params); };
  }

  Context() : state_{SLEEP}, bp{nullptr}, sp{nullptr}, fn_{} {}
  Context(const Context&) = delete;
  Context(Context&&) = delete;
  Context& operator=(const Context&) = delete;
  Context& operator=(Context&&) = delete;

  ~Context()
  {
    if(bp)
    {
      printf("free bp: %p\n", bp);
      free(bp);
    }
  }

  // this mean current running routine is `this` (caller is from)
  // so we saved current sp then switch to others'
  static void context_switch(Context* from, Context* to)
  {
    printf("ctxsw from %p to %p\n", from->sp, to->sp);
    from->set_status(Context::SLEEP);
    to->set_status(Context::RUNNING);
    switch_stack(&from->sp, to->sp, to);
  }

  void exec()
  {
    fn_();
    state_ = STOPPED;
    yield();
  }

  bool stopped() { return state_ == STOPPED; }

  bool running() { return state_ == RUNNING; }

  bool sleep() { return state_ == SLEEP; }

private:
  enum
  {
    RED_ZONE = 128,
    STACK_SIZE = 8192
  };
  Status state_;
  void* bp; // actually it's including RED_ZONE
  void* sp;
  std::function<void()> fn_;

  void set_status(Context::Status state) { state_ = state; }
};

template<typename T>
void ctx_function(T* obj)
{
  auto routine = static_cast<Context*>(obj);
  routine->exec();
}

class Scheduler final
{
  Scheduler() = default;

public:
  friend void yield();
  ~Scheduler()
  {
    for(auto iter = queue_.begin(); iter != queue_.end();)
    {
      delete *iter;
      iter = queue_.erase(iter);
    }
    queue_.clear();
    for(auto iter = stopped_.begin(); iter != stopped_.end();)
    {
      delete *iter;
      iter = stopped_.erase(iter);
    }
    stopped_.clear();
  }

  static Scheduler* instance()
  {
    static thread_local Scheduler sched;
    return &sched;
  }

  template<typename F, typename... Args>
  void spawn(F&& f, Args&&... args)
  {
    auto ctx = new Context{};
    ctx->init(std::forward<F>(f), std::forward<Args>(args)...);
    queue_.push_back(ctx);
  }

  void start()
  {
    if(this->queue_.empty()) { return; }
    Context* ctx = queue_.front();
    queue_.pop_front();
    queue_.push_back(ctx);
    init_signal(timer_);
    reset_timer(Scheduler::instance());
    Context::context_switch(&main_ctx_, ctx);
    // disable timer
    timer_delete(timer_);
    printf("back to main\n");
  }

private:
  Context main_ctx_{};
  Context empty_{};
  std::list<Context*> queue_{};
  std::list<Context*> stopped_{};
  timer_t timer_{};

  timer_t* timer() { return &timer_; }

  static void sched(int) //, siginfo_t*, void*)
  {
    reset_timer(Scheduler::instance());
    yield();
    Scheduler::instance()->clean_up();
  }

  static struct itimerspec* fixed_timeout()
  {
    static struct itimerspec ts
    {
      {0, 0}, { 0, 1000'000'00 }
    };
    return &ts;
  }

  static void init_signal(timer_t& t)
  {
    struct sigaction sa
    {
    };
    ::memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NODEFER;
    // sa.sa_sigaction = sched;
    sa.sa_handler = sched;

    struct sigevent sigev
    {
    };
    ::memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGRTMIN;
    sigev.sigev_value.sival_int = 0;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, nullptr);
    timer_create(CLOCK_REALTIME, &sigev, &t);
  }

  static void reset_timer(Scheduler* r) { timer_settime(r->timer(), 0, fixed_timeout(), nullptr); }

  void clean_up()
  {
    for(auto iter = queue_.begin(); iter != queue_.end();)
    {
      if((*iter)->stopped())
      {
        stopped_.push_back(*iter);
        iter = queue_.erase(iter);
      }
      else
      {
        ++iter;
      }
    }
  }

  // actually it's not safe in signal handler
  // context_switch will switch current running routine to another which has
  // state SLEEP, before that it must remove routines were STOPPED
  void context_switch()
  {
    clean_up();
    if(queue_.empty())
    {
      // last will saved last sp, and that maybe already de-allocated
      // it's safe, since we simple copy the pointer and never dereference.
      Context last;
      // back to main routine
      printf("back to start\n");
      Context::context_switch(&empty_, &main_ctx_);
    }

    // if queue_ is empty, it never goes here
    // here we switch to next subroutine
    Context* next = queue_.front();
    if(next->running()) { return; }
    if(queue_.size() == 1) { Context::context_switch(&empty_, next); }
    queue_.pop_front();
    Context* prev = queue_.back();
    if(!stopped_.empty()) { prev = stopped_.back(); }
    queue_.push_back(next);
    Context::context_switch(prev, next);
  }
};

void yield() { Scheduler::instance()->context_switch(); }

void spin()
{
  for(int i = 0; i < 10000000; ++i)
  {
    for(int j = 0; j < 10; ++j)
      ;
  }
}

void loop(const char* fmt)
{
  for(int i = 0; i < 3; ++i)
  {
    spin();
    printf(fmt, i);
  }
}

int main()
{
  Scheduler* sh = Scheduler::instance();
  sh->spawn(loop, "\033[33mfoo: %d\033[0m\n");
  sh->spawn(loop, "\033[32mbar: %d\033[0m\n");
  sh->spawn(loop, "\033[31m+1s: %d\033[0m\n");
  sh->start();
  printf("done\n");
}
