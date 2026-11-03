#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <time.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/proc.h>
#include <pthread.h>

extern char** environ;
static unsigned long long sequence;
static volatile sig_atomic_t interrupted;
static pid_t liveChild;
static pthread_mutex_t childLock = PTHREAD_MUTEX_INITIALIZER;
static double watchdogDeadline;
static void interruptedSignal(int sig) { __atomic_store_n(&interrupted, sig, __ATOMIC_RELAXED); }
static void cleanupChild(void) {
  pthread_mutex_lock(&childLock);
  if (liveChild <= 0) {
    pthread_mutex_unlock(&childLock);
    return;
  }
  siginfo_t info = {0};
  // Keep the child unreaped while cleaning its group: its PID cannot be reused.
  if (!waitid(P_PID, (id_t)liveChild, &info, WEXITED | WNOHANG | WNOWAIT)) {
    killpg(liveChild, SIGKILL);
    kill(liveChild, SIGKILL);
    while (waitpid(liveChild, NULL, 0) < 0 && errno == EINTR) {
    }
  }
  liveChild = 0;
  pthread_mutex_unlock(&childLock);
}

static double monotonic(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
static void* deadlineWatchdog(void* unused) {
  (void)unused;
  double shutdown = 0;
  for (;;) {
    double now = monotonic();
    pthread_mutex_lock(&childLock);
    if (liveChild <= 0) {
      pthread_mutex_unlock(&childLock);
      return NULL;
    }
    if (!shutdown && (now >= watchdogDeadline || __atomic_load_n(&interrupted, __ATOMIC_RELAXED)))
      shutdown = now;
    if (shutdown)
      killpg(liveChild, now - shutdown >= 2 ? SIGKILL : SIGTERM);
    pthread_mutex_unlock(&childLock);
    // Also bound a stalled AppKit observer, without using AppKit on this thread.
    // A missing final record remains inconclusive to the evidence evaluator.
    if (shutdown && now - shutdown >= 5) {
      cleanupChild();
      _exit(124);
    }
    usleep(20000);
  }
}
static NSDictionary* identity(pid_t pid) {
  struct proc_bsdinfo info = {0};
  char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
  int size = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
  if (size != (int)sizeof(info))
    return @{@"pid" : @(pid),
             @"read_ok" : @NO,
             @"read_errno" : @(errno)};
  int n = proc_pidpath(pid, path, sizeof(path));
  return @{@"pid" : @(pid),
           @"ppid" : @(info.pbi_ppid),
           @"pgid" : @(info.pbi_pgid),
           @"start_sec" : @(info.pbi_start_tvsec),
           @"start_usec" : @(info.pbi_start_tvusec),
           @"exe" : n > 0 ? @(path) : @"",
           @"read_ok" : @YES};
}
static NSString* key(NSDictionary* p) { return [p[@"read_ok"] boolValue] ? [NSString stringWithFormat:@"%@:%@:%@", p[@"pid"], p[@"start_sec"], p[@"start_usec"]] : @""; }
static NSArray* liveGroupMembers(pid_t group) {
  int count = proc_listallpids(NULL, 0);
  if (count <= 0)
    return nil;
  NSUInteger capacity = (NSUInteger)count + 128;
  NSMutableData* data = [NSMutableData dataWithLength:capacity * sizeof(pid_t)];
  int found = proc_listallpids(data.mutableBytes, (int)data.length);
  if (found <= 0)
    return nil;
  pid_t* pids = data.mutableBytes;
  NSMutableArray* members = [NSMutableArray array];
  for (int i = 0; i < found && (NSUInteger)i < capacity; ++i) {
    struct proc_bsdinfo info = {0};
    if (pids[i] != group && proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info) && info.pbi_pgid == (uint32_t)group && info.pbi_status != SZOMB)
      [members addObject:identity(pids[i])];
  }
  return members;
}
static NSDictionary* lineage(pid_t pid) {
  NSMutableArray* chain = [NSMutableArray array];
  NSMutableSet* visited = [NSMutableSet set];
  NSString* reason = @"terminal";
  for (int i = 0; i < 64 && pid > 1; ++i) {
    if ([visited containsObject:@(pid)]) {
      reason = @"cycle";
      break;
    }
    [visited addObject:@(pid)];
    NSDictionary* p = identity(pid);
    [chain addObject:p];
    if (![p[@"read_ok"] boolValue]) {
      reason = @"unreadable";
      break;
    }
    if (chain.count > 1) {
      NSDictionary* descendant = chain[chain.count - 2];
      unsigned long long parentStart = [p[@"start_sec"] unsignedLongLongValue] * 1000000 + [p[@"start_usec"] unsignedLongLongValue];
      unsigned long long childStart = [descendant[@"start_sec"] unsignedLongLongValue] * 1000000 + [descendant[@"start_usec"] unsignedLongLongValue];
      if (parentStart > childStart) {
        reason = @"ancestor_born_after_child";
        break;
      }
    }
    pid = [p[@"ppid"] intValue];
    if (i == 63 && pid > 1)
      reason = @"depth_limit";
  }
  if (chain.count && ![key(chain.firstObject) isEqual:key(identity([chain.firstObject[@"pid"] intValue]))])
    reason = @"identity_changed_during_sample";
  return @{@"chain" : chain, @"end_reason" : reason};
}
static void emit(NSFileHandle* handle, NSMutableDictionary* event) {
  event[@"schema"] = @1;
  event[@"sequence"] = @(sequence++);
  event[@"wall_time"] = @([[NSDate date] timeIntervalSince1970]);
  event[@"monotonic"] = @(monotonic());
  NSData* data = [NSJSONSerialization dataWithJSONObject:event options:NSJSONWritingSortedKeys error:nil];
  if (!data)
    [NSException raise:@"InvalidFocusRecord" format:@"cannot serialize observer record"];
  [handle writeData:data];
  [handle writeData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];
}
static void scanOwned(NSFileHandle* log, NSMutableSet* owned) {
  int count = proc_listallpids(NULL, 0);
  if (count <= 0)
    return;
  NSMutableData* buffer = [NSMutableData dataWithLength:(NSUInteger)(count + 128) * sizeof(pid_t)];
  int found = proc_listallpids(buffer.mutableBytes, (int)buffer.length);
  if (found <= 0)
    return;
  pid_t* pids = buffer.mutableBytes;
  NSMutableDictionary* snapshot = [NSMutableDictionary dictionary];
  for (int i = 0; i < found; ++i) {
    NSDictionary* p = identity(pids[i]);
    if ([p[@"read_ok"] boolValue])
      snapshot[p[@"pid"]] = p;
  }
  BOOL changed = YES;
  unsigned passes = 0;
  while (changed && passes++ < 64) {
    changed = NO;
    for (NSNumber* pid in snapshot) {
      NSDictionary* child = snapshot[pid];
      if ([owned containsObject:key(child)])
        continue;
      NSDictionary* parent = snapshot[child[@"ppid"]];
      if (!parent || ![owned containsObject:key(parent)])
        continue;
      unsigned long long ps = [parent[@"start_sec"] unsignedLongLongValue] * 1000000 + [parent[@"start_usec"] unsignedLongLongValue];
      unsigned long long cs = [child[@"start_sec"] unsignedLongLongValue] * 1000000 + [child[@"start_usec"] unsignedLongLongValue];
      if (ps > cs || ![key(parent) isEqual:key(identity([parent[@"pid"] intValue]))] || ![key(child) isEqual:key(identity([child[@"pid"] intValue]))])
        continue;
      [owned addObject:key(child)];
      changed = YES;
      emit(log, [@{@"kind" : @"owned_registered", @"identity" : child, @"parent" : parent, @"basis" : @"observed_parent_edge"} mutableCopy]);
    }
  }
}
int main(int argc, const char** argv) {
  @try {
    @autoreleasepool {
      if (argc < 4) {
        fprintf(stderr, "usage: observer out.jsonl absolute_executable timeout_seconds [args...]\n");
        return 2;
      }
      NSString* output = @(argv[1]);
      NSString* executable = [@(argv[2]) stringByResolvingSymlinksInPath];
      char* end = NULL;
      errno = 0;
      double limit = strtod(argv[3], &end);
      if (![output isAbsolutePath] || ![executable isAbsolutePath] || !end || *end || errno || !isfinite(limit) || limit <= 0)
        return 2;
      int logFD = open(output.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (logFD < 0) {
        perror("focus observer output");
        return 3;
      }
      NSFileHandle* log = [[NSFileHandle alloc] initWithFileDescriptor:logFD closeOnDealloc:YES];
      NSDictionary* selfIdentity = identity(getpid());
      emit(log, [@{@"kind" : @"observer_ready",
                   @"observer" : selfIdentity,
                   @"requested_executable" : executable,
                   @"poll_interval" : @0.02} mutableCopy]);
      NSWorkspace* workspace = NSWorkspace.sharedWorkspace;
      void (^record)(NSString*, NSRunningApplication*) = ^(NSString* source, NSRunningApplication* app) {
        NSMutableDictionary* event = [@{@"kind" : @"foreground",
                                        @"source" : source,
                                        @"pid" : @(app ? app.processIdentifier : 0),
                                        @"bundle" : app.bundleIdentifier ?: @"",
                                        @"app_executable" : app.executableURL.path ?: @"",
                                        @"app_launch_time" : app.launchDate ? @([app.launchDate timeIntervalSince1970]) : NSNull.null} mutableCopy];
        [event addEntriesFromDictionary:lineage(app ? app.processIdentifier : 0)];
        emit(log, event);
      };
      id observer = [workspace.notificationCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                                              object:nil
                                                               queue:NSOperationQueue.mainQueue
                                                          usingBlock:^(NSNotification* note) {
                                                            record(@"didActivate", note.userInfo[NSWorkspaceApplicationKey]);
                                                          }];
      record(@"before_launch", workspace.frontmostApplication);
      NSString *stdoutPath = [output stringByAppendingString:@".stdout"], *stderrPath = [output stringByAppendingString:@".stderr"];
      int outFD = open(stdoutPath.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      int errFD = open(stderrPath.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (outFD < 0 || errFD < 0) {
        if (outFD >= 0)
          close(outFD);
        if (errFD >= 0)
          close(errFD);
        return 3;
      }
      posix_spawnattr_t attrs;
      posix_spawn_file_actions_t actions;
      if (posix_spawnattr_init(&attrs) || posix_spawn_file_actions_init(&actions))
        return 4;
      sigset_t empty, defaults;
      sigemptyset(&empty);
      sigemptyset(&defaults);
      for (int sig = 1; sig < NSIG; ++sig)
        if (sig != SIGKILL && sig != SIGSTOP)
          sigaddset(&defaults, sig);
      int setup = posix_spawnattr_setflags(&attrs, POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
      setup |= posix_spawnattr_setpgroup(&attrs, 0);
      setup |= posix_spawnattr_setsigmask(&attrs, &empty);
      setup |= posix_spawnattr_setsigdefault(&attrs, &defaults);
      setup |= posix_spawn_file_actions_adddup2(&actions, outFD, STDOUT_FILENO);
      setup |= posix_spawn_file_actions_adddup2(&actions, errFD, STDERR_FILENO);
      setup |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
      char** childArgs = calloc((size_t)argc - 2, sizeof(char*));
      if (!childArgs)
        return 4;
      childArgs[0] = (char*)executable.fileSystemRepresentation;
      for (int i = 4; i < argc; ++i)
        childArgs[i - 3] = (char*)argv[i];
      signal(SIGCHLD, SIG_DFL);
      atexit(cleanupChild);
      pid_t child = 0;
      int launchError = setup ? setup : posix_spawn(&child, executable.fileSystemRepresentation, &actions, &attrs, childArgs, environ);
      free(childArgs);
      posix_spawnattr_destroy(&attrs);
      posix_spawn_file_actions_destroy(&actions);
      close(outFD);
      close(errFD);
      if (launchError) {
        emit(log, [@{@"kind" : @"launch_failed",
                     @"error" : @(strerror(launchError))} mutableCopy]);
        return 4;
      }
      pthread_mutex_lock(&childLock);
      liveChild = child;
      pthread_mutex_unlock(&childLock);
      // Capture identity while suspended, before even a short-lived child can exit.
      NSDictionary* root = identity(child);
      emit(log, [@{@"kind" : @"child_started",
                   @"spawned_pid" : @(child),
                   @"root" : root} mutableCopy]);
      // A script can start in its interpreter. Ownership comes from the
      // kernel spawn result and birth identity, not a guessed image name.
      if (![root[@"read_ok"] boolValue] || [root[@"pgid"] intValue] != child || ![root[@"exe"] length]) {
        return 4;
      }
      struct sigaction handler = {0};
      handler.sa_handler = interruptedSignal;
      sigemptyset(&handler.sa_mask);
      sigaction(SIGINT, &handler, NULL);
      sigaction(SIGTERM, &handler, NULL);
      sigaction(SIGHUP, &handler, NULL);
      NSMutableSet* owned = [NSMutableSet setWithObjects:key(selfIdentity), key(root), nil];
      double began = monotonic(), last = began, maxGap = 0, endSeen = 0, lastCensus = 0, terminationBegan = 0;
      unsigned polls = 0;
      BOOL timedOut = NO, running = YES, orphans = NO;
      NSString* previous = @"";
      int childStatus = 0;
      watchdogDeadline = began + limit;
      pthread_t watchdog;
      if (pthread_create(&watchdog, NULL, deadlineWatchdog, NULL))
        return 4;
      pthread_detach(watchdog);
      kill(child, SIGCONT);
      while (YES) {
        @autoreleasepool {
          double now = monotonic();
          maxGap = fmax(maxGap, now - last);
          last = now;
          ++polls;
          if (now - lastCensus >= 0.1) {
            scanOwned(log, owned);
            lastCensus = now;
          }
          NSRunningApplication* app = workspace.frontmostApplication;
          NSString* current = [NSString stringWithFormat:@"%d:%@", app ? app.processIdentifier : 0, app.launchDate ?: @""];
          if (![current isEqual:previous]) {
            record(@"poll_transition", app);
            previous = current;
          }
          if (running) {
            // Leave the exited root unreaped until its process group is drained.
            // This reserves its PID/PGID, so cleanup cannot hit a reused user PID.
            siginfo_t info = {0};
            int rc = waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT);
            if (!rc && info.si_pid == child)
              running = NO;
            else if (rc < 0 && errno != EINTR)
              return 4;
          }
          if (running && !terminationBegan && (now - began > limit || __atomic_load_n(&interrupted, __ATOMIC_RELAXED))) {
            timedOut = now - began > limit;
            terminationBegan = now;
            killpg(child, SIGTERM);
          }
          if (terminationBegan && now - terminationBegan > 2)
            killpg(child, SIGKILL);
          if (!running) {
            if (endSeen == 0)
              endSeen = now;
            NSArray* members = liveGroupMembers(child);
            if (!members)
              return 4;
            if (members.count && !orphans) {
              orphans = YES;
              emit(log, [@{@"kind" : @"orphaned_group",
                           @"members" : members,
                           @"root" : root,
                           @"root_unreaped" : @YES} mutableCopy]);
              if (!terminationBegan)
                terminationBegan = now;
              killpg(child, SIGTERM);
            }
            if (now - endSeen >= 0.25 && !members.count)
              break;
          }
          [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
      }
      record(@"after_exit", workspace.frontmostApplication);
      [workspace.notificationCenter removeObserver:observer];
      pthread_mutex_lock(&childLock);
      if (waitpid(child, &childStatus, 0) != child) {
        pthread_mutex_unlock(&childLock);
        return 4;
      }
      liveChild = 0;
      pthread_mutex_unlock(&childLock);
      int exitCode = WIFEXITED(childStatus) ? WEXITSTATUS(childStatus) : 128 + WTERMSIG(childStatus);
      emit(log, [@{@"kind" : @"observer_finished",
                   @"child_exit" : @(exitCode),
                   @"timed_out" : @(timedOut),
                   @"interrupted_signal" : @(__atomic_load_n(&interrupted, __ATOMIC_RELAXED)),
                   @"orphaned_group" : @(orphans),
                   @"polls" : @(polls),
                   @"max_poll_gap_seconds" : @(maxGap),
                   @"duration_seconds" : @(monotonic() - began)} mutableCopy]);
      [log closeFile];
      int interruption = __atomic_load_n(&interrupted, __ATOMIC_RELAXED);
      return timedOut ? 124 : interruption ? 128 + interruption
                          : orphans        ? 125
                                           : exitCode;
    }
  } @catch (NSException* exception) {
    cleanupChild();
    fprintf(stderr, "focus observer: %s\n", exception.reason.UTF8String);
    return 125;
  }
}
