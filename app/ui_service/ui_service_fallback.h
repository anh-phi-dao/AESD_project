#ifndef UI_SERVICE_FALLBACK_H_
#define UI_SERVICE_FALLBACK_H_

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Built only when app/ui_service/ui/ holds no SquareLine export. Same name as SquareLine's entry point, so
 * ui_service.c calls ui_init() either way. */
#ifdef __cplusplus
extern "C"
{
#endif

    void ui_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_FALLBACK_H_ */
