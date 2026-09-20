// S3 static version header (Task 5). The Pico build generates headers/version.h
// from version.h.in at configure time; the S3 build has no Pico CMake step,
// so this committed file provides the same macros. Values are updated by hand
// when the PoC moves (base = first S3 bring-up commit series).
// OWNERSHIP: interim only — replaced-by-generation in Task 3c (CMake-generated
// version.h for the S3 build); do not add macros here without a consumer.
#define GP2040VERSION "v1.0.0-s3-poc"
#define GP2040VERSIONID "945dfeb-task5"
#define GP2040BUILD "idf"
#define GP2040CONFIG "S3"
#define GP2040PLATFORM "esp32s3"
