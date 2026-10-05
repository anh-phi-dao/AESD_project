/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <string.h>
#include "remote_service_json.h"
#include "cJSON.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Largest integer a JSON number (IEEE double) holds exactly; also bounds before_id to uint32. */
#define REMOTE_SERVICE_JSON_U32_MAX 4294967295.0
#define REMOTE_SERVICE_JSON_UNKNOWN "unknown"

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static cJSON *remote_service_json_event_obj(const event_log_event_t *const p_event);
static cJSON *remote_service_json_alert_obj(const event_log_alert_t *const p_alert);
static char  *remote_service_json_print(cJSON *const p_root);
static bool   remote_service_json_is_req_id(const char *const p_id);
static bool   remote_service_json_whole_number(const cJSON *const p_item, double min, double max);

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Encodes one event: {"id","ts","method","result"[,"user"]}.
 **********************************************************************************************************************/
char *remote_service_json_event(const event_log_event_t *const p_event)
{
    return remote_service_json_print(remote_service_json_event_obj(p_event));
}

/***********************************************************************************************************************
 * Encodes one alert: {"id","ts","level","code"[,"msg"]}.
 **********************************************************************************************************************/
char *remote_service_json_alert(const event_log_alert_t *const p_alert)
{
    return remote_service_json_print(remote_service_json_alert_obj(p_alert));
}

/***********************************************************************************************************************
 * Encodes the retained snapshot of the most recent entries.
 **********************************************************************************************************************/
char *remote_service_json_recent(uint32_t                       gen,
                                 const event_log_event_t *const p_events,
                                 size_t                         event_count,
                                 const event_log_alert_t *const p_alerts,
                                 size_t                         alert_count)
{
    cJSON *p_root       = cJSON_CreateObject();
    cJSON *p_events_arr = cJSON_CreateArray();
    cJSON *p_alerts_arr = cJSON_CreateArray();
    bool   ok           = (p_root != NULL) && (p_events_arr != NULL) && (p_alerts_arr != NULL);

    if (ok)
    {
        ok = (cJSON_AddNumberToObject(p_root, "gen", (double)gen) != NULL);
        cJSON_AddItemToObject(p_root, "events", p_events_arr);
        cJSON_AddItemToObject(p_root, "alerts", p_alerts_arr);
    }
    else
    {
        cJSON_Delete(p_events_arr);
        cJSON_Delete(p_alerts_arr);
    }
    for (size_t i = 0U; ok && (i < event_count); i++)
    {
        cJSON *p_item = remote_service_json_event_obj(&p_events[i]);
        ok            = (p_item != NULL) && cJSON_AddItemToArray(p_events_arr, p_item);
    }
    for (size_t i = 0U; ok && (i < alert_count); i++)
    {
        cJSON *p_item = remote_service_json_alert_obj(&p_alerts[i]);
        ok            = (p_item != NULL) && cJSON_AddItemToArray(p_alerts_arr, p_item);
    }

    if (!ok)
    {
        cJSON_Delete(p_root);
        return NULL;
    }
    return remote_service_json_print(p_root);
}

/***********************************************************************************************************************
 * Encodes one history/resp page.
 **********************************************************************************************************************/
char *remote_service_json_history(const char *const              p_req_id,
                                  const event_log_event_t *const p_items,
                                  size_t                         item_count,
                                  bool                           more)
{
    cJSON *p_root  = cJSON_CreateObject();
    cJSON *p_array = cJSON_CreateArray();
    bool   ok      = (p_root != NULL) && (p_array != NULL);

    if (ok)
    {
        ok = (cJSON_AddStringToObject(p_root, "req_id", p_req_id) != NULL);
        cJSON_AddItemToObject(p_root, "items", p_array);
        ok = ok && (cJSON_AddBoolToObject(p_root, "more", more) != NULL);
    }
    else
    {
        cJSON_Delete(p_array);
    }
    for (size_t i = 0U; ok && (i < item_count); i++)
    {
        cJSON *p_item = remote_service_json_event_obj(&p_items[i]);
        ok            = (p_item != NULL) && cJSON_AddItemToArray(p_array, p_item);
    }

    if (!ok)
    {
        cJSON_Delete(p_root);
        return NULL;
    }
    return remote_service_json_print(p_root);
}

/***********************************************************************************************************************
 * Parses {"req_id":"..","before_id":N,"limit":N}. Only req_id is required.
 *
 * req_id is echoed into the response, so only short [A-Za-z0-9_-] ids are accepted. A limit outside 1..50 is clamped
 * as the protocol says; a before_id that is not a positive whole number rejects the request.
 **********************************************************************************************************************/
bool remote_service_json_parse_history_req(const char *const                   p_json,
                                           size_t                              len,
                                           remote_service_history_req_t *const p_req)
{
    if ((p_json == NULL) || (p_req == NULL) || (len == 0U) || (len > REMOTE_SERVICE_HISTORY_REQ_MAX_LEN))
    {
        return false;
    }

    cJSON *p_root = cJSON_ParseWithLength(p_json, len);
    if (!cJSON_IsObject(p_root))
    {
        cJSON_Delete(p_root);
        return false;
    }

    const cJSON *p_id     = cJSON_GetObjectItemCaseSensitive(p_root, "req_id");
    const cJSON *p_before = cJSON_GetObjectItemCaseSensitive(p_root, "before_id");
    const cJSON *p_limit  = cJSON_GetObjectItemCaseSensitive(p_root, "limit");

    bool ok = cJSON_IsString(p_id) && remote_service_json_is_req_id(p_id->valuestring);
    ok = ok && ((p_before == NULL) || remote_service_json_whole_number(p_before, 1.0, REMOTE_SERVICE_JSON_U32_MAX));
    ok = ok && ((p_limit == NULL) || cJSON_IsNumber(p_limit));

    if (ok)
    {
        memset(p_req, 0, sizeof(*p_req));
        (void)strcpy(p_req->req_id, p_id->valuestring);
        p_req->before_id = (p_before != NULL) ? (uint32_t)p_before->valuedouble : EVENT_LOG_NEWEST;

        double limit = (p_limit != NULL) ? p_limit->valuedouble : (double)REMOTE_SERVICE_HISTORY_LIMIT_DEFAULT;
        if (!(limit >= 1.0)) /* Also catches NaN. */
        {
            limit = 1.0;
        }
        if (limit > (double)REMOTE_SERVICE_HISTORY_LIMIT_MAX)
        {
            limit = (double)REMOTE_SERVICE_HISTORY_LIMIT_MAX;
        }
        p_req->limit = (uint8_t)limit;
    }

    cJSON_Delete(p_root);
    return ok;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Builds the object for one event, or NULL when out of memory.
 **********************************************************************************************************************/
static cJSON *remote_service_json_event_obj(const event_log_event_t *const p_event)
{
    const char *p_method = event_log_method_name((event_log_method_t)p_event->method);
    const char *p_result = event_log_result_name((event_log_result_t)p_event->result);
    cJSON      *p_obj    = cJSON_CreateObject();

    bool ok = (p_obj != NULL) && (cJSON_AddNumberToObject(p_obj, "id", (double)p_event->id) != NULL)
              && (cJSON_AddNumberToObject(p_obj, "ts", (double)p_event->ts) != NULL)
              && (cJSON_AddStringToObject(p_obj, "method", (p_method != NULL) ? p_method : REMOTE_SERVICE_JSON_UNKNOWN)
                  != NULL)
              && (cJSON_AddStringToObject(p_obj, "result", (p_result != NULL) ? p_result : "denied") != NULL)
              && ((p_event->user[0] == '\0') || (cJSON_AddStringToObject(p_obj, "user", p_event->user) != NULL));

    if (!ok)
    {
        cJSON_Delete(p_obj);
        return NULL;
    }
    return p_obj;
}

/***********************************************************************************************************************
 * Builds the object for one alert, or NULL when out of memory.
 **********************************************************************************************************************/
static cJSON *remote_service_json_alert_obj(const event_log_alert_t *const p_alert)
{
    const char *p_level = event_log_level_name((event_log_level_t)p_alert->level);
    const char *p_code  = event_log_alert_code_name((event_log_alert_code_t)p_alert->code);
    cJSON      *p_obj   = cJSON_CreateObject();

    bool ok
        = (p_obj != NULL) && (cJSON_AddNumberToObject(p_obj, "id", (double)p_alert->id) != NULL)
          && (cJSON_AddNumberToObject(p_obj, "ts", (double)p_alert->ts) != NULL)
          && (cJSON_AddStringToObject(p_obj, "level", (p_level != NULL) ? p_level : "warning") != NULL)
          && (cJSON_AddStringToObject(p_obj, "code", (p_code != NULL) ? p_code : REMOTE_SERVICE_JSON_UNKNOWN) != NULL)
          && ((p_alert->msg[0] == '\0') || (cJSON_AddStringToObject(p_obj, "msg", p_alert->msg) != NULL));

    if (!ok)
    {
        cJSON_Delete(p_obj);
        return NULL;
    }
    return p_obj;
}

/***********************************************************************************************************************
 * Prints compact JSON and frees the tree. Returns NULL when p_root is NULL or printing runs out of memory.
 **********************************************************************************************************************/
static char *remote_service_json_print(cJSON *const p_root)
{
    if (p_root == NULL)
    {
        return NULL;
    }
    char *p_text = cJSON_PrintUnformatted(p_root);
    cJSON_Delete(p_root);
    return p_text;
}

/***********************************************************************************************************************
 * Accepts 1..REMOTE_SERVICE_HISTORY_REQ_ID_MAX_LEN characters of [A-Za-z0-9_-].
 **********************************************************************************************************************/
static bool remote_service_json_is_req_id(const char *const p_id)
{
    const size_t len = strlen(p_id);
    if ((len == 0U) || (len > REMOTE_SERVICE_HISTORY_REQ_ID_MAX_LEN))
    {
        return false;
    }
    for (size_t i = 0U; i < len; i++)
    {
        const char c = p_id[i];
        if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '-')
              || (c == '_')))
        {
            return false;
        }
    }
    return true;
}

/***********************************************************************************************************************
 * True when the item is a number without a fractional part within [min, max].
 **********************************************************************************************************************/
static bool remote_service_json_whole_number(const cJSON *const p_item, double min, double max)
{
    if (!cJSON_IsNumber(p_item))
    {
        return false;
    }
    const double v = p_item->valuedouble;
    return (v >= min) && (v <= max) && (v == (double)(uint32_t)v);
}
