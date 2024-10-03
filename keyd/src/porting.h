#include "pico/stdlib.h"


#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))

#define keyd_log(fmt, ...) printf(fmt "\n", ##__VA_ARGS__);
// #define keyd_log(fmt, ...) _keyd_log(0, fmt, ##__VA_ARGS__);
#define dbg(fmt, ...) printf(fmt "\n", ##__VA_ARGS__);
// #define dbg(fmt, ...) _keyd_log(1, "r{DEBUG:} b{%s:%d:} "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define dbg2(fmt, ...) printf(fmt "\n", ##__VA_ARGS__);
// #define dbg2(fmt, ...) _keyd_log(2, "r{DEBUG:} b{%s:%d:} "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define err(fmt, ...) printf(fmt "\n", ##__VA_ARGS__);
#define warn(fmt, ...) printf(fmt "\n", ##__VA_ARGS__);

void usleep(uint32_t timeout_us);