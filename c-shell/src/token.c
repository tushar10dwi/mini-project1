#include "token.h"

#include <stdlib.h>
#include <string.h>

token_t *token_list_append(token_t **head, token_type_t type, const char *value)
{
    token_t *node = malloc(sizeof(token_t));
    if (node == NULL) {
        return NULL;
    }

    node->type = type;
    node->value = NULL;
    node->next = NULL;

    if (value != NULL) {
        node->value = strdup(value);
        if (node->value == NULL) {
            free(node);
            return NULL;
        }
    }

    if (*head == NULL) {
        *head = node;
    } else {
        token_t *tail = *head;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = node;
    }

    return node;
}

void token_list_free(token_t **head)
{
    token_t *cur = *head;
    while (cur != NULL) {
        token_t *next = cur->next;
        free(cur->value);
        free(cur);
        cur = next;
    }
    *head = NULL;
}