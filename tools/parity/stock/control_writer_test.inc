/* Owned sink controls only. Never compiled into the Stock producer. */
static DWORD writer_test_delay;
static LONG writer_test_fail_once;
void control_tape_test_writer(DWORD delay,int fail_once) {
  writer_test_delay=delay;writer_test_fail_once=fail_once;
}
static int writer_flush(FILE* f) {
  if(writer_test_delay) Sleep(writer_test_delay);
  if(InterlockedExchange(&writer_test_fail_once,0)) return EOF;
  return fflush(f);
}
