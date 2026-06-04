#include <demo.h>
#include <stdio.h>
#include <string.h>

// clang-format off
model_group_info_t model_config[] = {
    { .model = MODEL_1, 
      .series_info = {
          { MODEL_1_1, MODEL_1_1_N }, 
          { MODEL_1_2, MODEL_1_2_N }, 
          { MODEL_1_3, MODEL_1_3_N }
      }, 
      .series_count = 3 
    },
    { .model = MODEL_2, 
      .series_info = {
          { MODEL_2_1, MODEL_2_1_N }, 
          { MODEL_2_2, MODEL_2_2_N }
      }, 
      .series_count = 2 
    },
};

// clang-format off
#define MEM_NUM (sizeof(model_config) / sizeof(model_config[0]))

// Function to parse min and max from the refer string
void parse_min_max(const char *refer, int *max, int *min) {
    sscanf(refer, "%*[^_]_%d_%d", max, min);
}

// Function to find and print the name, min, and max for a given input
void print_model_info(const char *input) {
    for (int i = 0; i < MEM_NUM; i++) {
        for (int j = 0; j < model_config[i].series_count; j++) {
            const char *refer = model_config[i].series_info[j].refer;
            if (strstr(refer, input) != NULL) {  // Check if the input is in the refer string
                int max = 0, min = 0;
                char name[50];
                sscanf(refer, "%[^_]_%*d_%*d", name);  // Extract the correct name part
                parse_min_max(refer, &max, &min);
                printf("name: %s\n", name);
                printf("min: %d\n", min);
                printf("max: %d\n", max);
                return;
            }
        }
    }
    printf("Model not found for input: %s\n", input);
}

int test_main() {
  const char *input = "1_1";
  print_model_info(input);
  return 0;
}
