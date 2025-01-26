#include "email.h"
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include "../value.h"
#include "../deps/mustach/mustach-jansson.h"

// Helper function to find email template by name
static EmailTemplateNode* findEmailTemplate(const char *name, ServerContext *ctx) {
    if (!ctx || !ctx->website || !ctx->website->email) {
        return NULL;
    }
    
    EmailTemplateNode *current = ctx->website->email->templateHead;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
static int sendEmail(Arena *arena, const char *to, const char *subject, const char *htmlContent, ServerContext *ctx) {
    if (!ctx || !ctx->website || !ctx->website->email || !ctx->website->email->sendgrid) {
        fprintf(stderr, "Email configuration not found\n");
        return -1;
    }

    SendGridNode *config = ctx->website->email->sendgrid;
    
    // Get API key
    const char *apiKey = resolveString(arena, &config->apiKey);
    if (!apiKey) {
        fprintf(stderr, "SendGrid API key not found\n");
        return -1;
    }
    
    // Get from email and name
    const char *fromEmail = resolveString(arena, &config->fromEmail);
    const char *fromName = resolveString(arena, &config->fromName);
    if (!fromEmail) {
        fprintf(stderr, "SendGrid from email not found\n");
        return -1;
    }

    // Build SendGrid API request JSON
    json_t *request = json_object();
    
    // From
    json_t *from = json_object();
    json_object_set_new(from, "email", json_string(fromEmail));
    if (fromName) {
        json_object_set_new(from, "name", json_string(fromName));
    }
    json_object_set_new(request, "from", from);
    
    // To
    json_t *toArray = json_array();
    json_t *toObject = json_object();
    json_object_set_new(toObject, "email", json_string(to));
    json_array_append_new(toArray, toObject);
    json_object_set_new(request, "personalizations", toArray);
    
    // Subject
    json_object_set_new(request, "subject", json_string(subject));
    
    // Content
    json_t *content = json_array();
    json_t *htmlContentObj = json_object();
    json_object_set_new(htmlContentObj, "type", json_string("text/html"));
    json_object_set_new(htmlContentObj, "value", json_string(htmlContent));
    json_array_append_new(content, htmlContentObj);
    json_object_set_new(request, "content", content);

    // Convert request to string
    char *requestStr = json_dumps(request, JSON_COMPACT);
    json_decref(request);

    // Initialize curl
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(requestStr);
        fprintf(stderr, "Failed to initialize curl\n");
        return -1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char authHeader[256];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", apiKey);
    headers = curl_slist_append(headers, authHeader);

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.sendgrid.com/v3/mail/send");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(requestStr);

    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send email: %s\n", curl_easy_strerror(res));
        return -1;
    }

    return 0;
}
#pragma clang diagnostic pop

int sendVerificationEmail(ServerContext *ctx, Arena *arena, const char *email, const char *verificationUrl) {
    EmailTemplateNode *tmpl = findEmailTemplate("verification", ctx);
    if (!tmpl) {
        fprintf(stderr, "Verification email template not found\n");
        return -1;
    }

    // Create JSON object with template variables
    json_t *vars = json_object();
    json_object_set_new(vars, "verificationUrl", json_string(verificationUrl));

    // Render template
    char *result = NULL;
    size_t result_size = 0;
    int rc = mustach_jansson_mem(tmpl->template->content, 
                                strlen(tmpl->template->content),
                                vars, 
                                Mustach_With_AllExtensions,
                                &result,
                                &result_size);
    
    json_decref(vars);

    if (rc != MUSTACH_OK) {
        fprintf(stderr, "Failed to render verification email template\n");
        return -1;
    }

    // Send email
    rc = sendEmail(arena, email, tmpl->subject, result, ctx);
    free(result);
    
    return rc;
}

int sendPasswordResetEmail(ServerContext *ctx, Arena *arena, const char *email, const char *resetUrl) {
    EmailTemplateNode *tmpl = findEmailTemplate("passwordReset", ctx);
    if (!tmpl) {
        fprintf(stderr, "Password reset email template not found\n");
        return -1;
    }

    // Create JSON object with template variables
    json_t *vars = json_object();
    json_object_set_new(vars, "resetUrl", json_string(resetUrl));

    // Render template
    char *result = NULL;
    size_t result_size = 0;
    int rc = mustach_jansson_mem(tmpl->template->content,
                                strlen(tmpl->template->content),
                                vars,
                                Mustach_With_AllExtensions,
                                &result,
                                &result_size);
    
    json_decref(vars);

    if (rc != MUSTACH_OK) {
        fprintf(stderr, "Failed to render password reset email template\n");
        return -1;
    }

    // Send email
    rc = sendEmail(arena, email, tmpl->subject, result, ctx);
    free(result);
    
    return rc;
}
