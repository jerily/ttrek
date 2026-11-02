/* Registry transport used only by the CLI integration tests. */
#include <stdlib.h>
#include <string.h>
#include "registry.h"

int ttrek_RegistryGet(const char *url, Tcl_DString *result, cJSON *post_data) {
    const char *filename = getenv("TTREK_TEST_REGISTRY");
    if (!filename || post_data) { return TCL_ERROR; }
    FILE *file = fopen(filename, "rb");
    if (!file) { return TCL_ERROR; }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *data = malloc(size + 1);
    size_t read_size = fread(data, 1, size, file);
    fclose(file);
    data[read_size] = '\0';
    cJSON *fixture = cJSON_Parse(data);
    free(data);
    const char *path = strstr(url, "/registry/");
    cJSON *response = path ? cJSON_GetObjectItemCaseSensitive(fixture, path) : NULL;
    int status = TCL_ERROR;
    if (response) {
        char *json = cJSON_PrintUnformatted(response);
        Tcl_DStringAppend(result, json, -1);
        cJSON_free(json);
        status = TCL_OK;
    }
    cJSON_Delete(fixture);
    return status;
}
