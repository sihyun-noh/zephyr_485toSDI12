

#ifndef __DEMO_H__
#define __DEMO_H__

typedef enum {
  MODEL_1,
  MODEL_2,
  MODEL_3,
  MODEL_4,
  MODEL_5,
} model_group_t;

typedef enum {
  MODEL_1_1,
  MODEL_1_2,
  MODEL_1_3,
  MODEL_2_1,
  MODEL_2_2,
} model_series_t;

typedef struct {
  model_series_t series;
  const char *refer;
} model_series_info_t;

typedef struct {
  model_group_t model;
  model_series_info_t series_info[3]; // 최대 시리즈 수를 미리 지정
  int series_count;
  int min;
  int max;
} model_group_info_t;

#define MODEL_1_1_N "model/1/1_200_20"
#define MODEL_1_2_N "model_1_2_2000_100"
#define MODEL_1_3_N "model_1_3_30000_1000"
#define MODEL_2_1_N "model_2_1_100_0.1"
#define MODEL_2_2_N "model_2_2_300_0.01"

int test_main();
#endif
