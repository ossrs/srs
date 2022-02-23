
#include "yaml_private.h"

/*
 * Flush the buffer if needed.
 */

#define FLUSH(emitter)                                                          \
    ((emitter->buffer.pointer+5 < emitter->buffer.end)                          \
     || yaml_emitter_flush(emitter))

/*
 * Put a character to the output buffer.
 */

#define PUT(emitter,value)                                                      \
    (FLUSH(emitter)                                                             \
     && (*(emitter->buffer.pointer++) = (yaml_char_t)(value),                   \
         emitter->column++,                                             	\
         1))

/*
 * Put a line break to the output buffer.
 */

#define PUT_BREAK(emitter)                                                      \
    (FLUSH(emitter)                                                             \
     && ((emitter->line_break == YAML_CR_BREAK ?                                \
             (*(emitter->buffer.pointer++) = (yaml_char_t) '\r') :              \
          emitter->line_break == YAML_LN_BREAK ?                                \
             (*(emitter->buffer.pointer++) = (yaml_char_t) '\n') :              \
          emitter->line_break == YAML_CRLN_BREAK ?                              \
             (*(emitter->buffer.pointer++) = (yaml_char_t) '\r',                \
              *(emitter->buffer.pointer++) = (yaml_char_t) '\n') : 0),          \
         emitter->column = 0,                                                   \
         emitter->line ++,                                                      \
         1))

/*
 * Copy a character from a string into buffer.
 */

#define WRITE(emitter,string)                                                   \
    (FLUSH(emitter)                                                             \
     && (COPY(emitter->buffer,string),                                          \
         emitter->column ++,                                                    \
         1))

/*
 * Copy a line break character from a string into buffer.
 */

#define WRITE_BREAK(emitter,string)                                             \
    (FLUSH(emitter)                                                             \
     && (CHECK(string,'\n') ?                                                   \
         (PUT_BREAK(emitter),                                                   \
          string.pointer ++,                                                    \
          1) :                                                                  \
         (COPY(emitter->buffer,string),                                         \
          emitter->column = 0,                                                  \
          emitter->line ++,                                                     \
          1)))

/*
 * API functions.
 */

YAML_DECLARE(int)
yaml_emitter_emit(yaml_emitter_t *emitter, yaml_event_t *event);

/*
 * Utility functions.
 */

static int
yaml_emitter_set_emitter_error(yaml_emitter_t *emitter, const char *problem);

static int
yaml_emitter_need_more_events(yaml_emitter_t *emitter);

static int
yaml_emitter_append_tag_directive(yaml_emitter_t *emitter,
        yaml_tag_directive_t value, int allow_duplicates);

static int
yaml_emitter_increase_indent(yaml_emitter_t *emitter,
        int flow, int indentless);

/*
 * State functions.
 */

static int
yaml_emitter_state_machine(yaml_emitter_t *emitter, yaml_event_t *event);

static int
yaml_emitter_emit_stream_start(yaml_emitter_t *emitter,
        yaml_event_t *event);

static int
yaml_emitter_emit_document_start(yaml_emitter_t *emitter,
        yaml_event_t *event, int first);

static int
yaml_emitter_emit_document_content(yaml_emitter_t *emitter,
        yaml_event_t *event);

static int
yaml_emitter_emit_document_end(yaml_emitter_t *emitter,
        yaml_event_t *event);

static int
yaml_emitter_emit_flow_sequence_item(yaml_emitter_t *emitter,
        yaml_event_t *event, int first);

static int
yaml_emitter_emit_flow_mapping_key(yaml_emitter_t *emitter,
        yaml_event_t *event, int first);

static int
yaml_emitter_emit_flow_mapping_value(yaml_emitter_t *emitter,
        yaml_event_t *event, int simple);

static int
yaml_emitter_emit_block_sequence_item(yaml_emitter_t *emitter,
        yaml_event_t *event, int first);

static int
yaml_emitter_emit_block_mapping_key(yaml_emitter_t *emitter,
        yaml_event_t *event, int first);

static int
yaml_emitter_emit_block_mapping_value(yaml_emitter_t *emitter,
        yaml_event_t *event, int simple);

static int
yaml_emitter_emit_node(yaml_emitter_t *emitter, yaml_event_t *event,
        int root, int sequence, int mapping, int simple_key);

static int
yaml_emitter_emit_alias(yaml_emitter_t *emitter, yaml_event_t *event);

static int
yaml_emitter_emit_scalar(yaml_emitter_t *emitter, yaml_event_t *event);

static int
yaml_emitter_emit_sequence_start(yaml_emitter_t *emitter, yaml_event_t *event);

static int
yaml_emitter_emit_mapping_start(yaml_emitter_t *emitter, yaml_event_t *event);

/*
 * Checkers.
 */

static int
yaml_emitter_check_empty_document(yaml_emitter_t *emitter);

static int
yaml_emitter_check_empty_sequence(yaml_emitter_t *emitter);

static int
yaml_emitter_check_empty_mapping(yaml_emitter_t *emitter);

static int
yaml_emitter_check_simple_key(yaml_emitter_t *emitter);

static int
yaml_emitter_select_scalar_style(yaml_emitter_t *emitter, yaml_event_t *event);

/*
 * Processors.
 */

static int
yaml_emitter_process_anchor(yaml_emitter_t *emitter);

static int
yaml_emitter_process_tag(yaml_emitter_t *emitter);

static int
yaml_emitter_process_scalar(yaml_emitter_t *emitter);

/*
 * Analyzers.
 */

static int
yaml_emitter_analyze_version_directive(yaml_emitter_t *emitter,
        yaml_version_directive_t version_directive);

static int
yaml_emitter_analyze_tag_directive(yaml_emitter_t *emitter,
        yaml_tag_directive_t tag_directive);

static int
yaml_emitter_analyze_anchor(yaml_emitter_t *emitter,
        yaml_char_t *anchor, int alias);

static int
yaml_emitter_analyze_tag(yaml_emitter_t *emitter,
        yaml_char_t *tag);

static int
yaml_emitter_analyze_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length);

static int
yaml_emitter_analyze_event(yaml_emitter_t *emitter,
        yaml_event_t *event);

/*
 * Writers.
 */

static int
yaml_emitter_write_bom(yaml_emitter_t *emitter);

static int
yaml_emitter_write_indent(yaml_emitter_t *emitter);

static int
yaml_emitter_write_indicator(yaml_emitter_t *emitter,
        const char *indicator, int need_whitespace,
        int is_whitespace, int is_indention);

static int
yaml_emitter_write_anchor(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length);

static int
yaml_emitter_write_tag_handle(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length);

static int
yaml_emitter_write_tag_content(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int need_whitespace);

static int
yaml_emitter_write_plain_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks);

static int
yaml_emitter_write_single_quoted_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks);

static int
yaml_emitter_write_double_quoted_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks);

static int
yaml_emitter_write_block_scalar_hints(yaml_emitter_t *emitter,
        yaml_string_t string);

static int
yaml_emitter_write_literal_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length);

static int
yaml_emitter_write_folded_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length);

/*
 * Set an emitter error and return 0.
 */

static int
yaml_emitter_set_emitter_error(yaml_emitter_t *emitter, const char *problem)
{
    emitter->error = YAML_EMITTER_ERROR;
    emitter->problem = problem;

    return 0;
}

/*
 * Emit an event.
 */

YAML_DECLARE(int)
yaml_emitter_emit(yaml_emitter_t *emitter, yaml_event_t *event)
{
    if (!ENQUEUE(emitter, emitter->events, *event)) {
        yaml_event_delete(event);
        return 0;
    }

    while (!yaml_emitter_need_more_events(emitter)) {
        if (!yaml_emitter_analyze_event(emitter, emitter->events.head))
            return 0;
        if (!yaml_emitter_state_machine(emitter, emitter->events.head))
            return 0;
        yaml_event_delete(&DEQUEUE(emitter, emitter->events));
    }

    return 1;
}

/*
 * Check if we need to accumulate more events before emitting.
 *
 * We accumulate extra
 *  - 1 event for DOCUMENT-START
 *  - 2 events for SEQUENCE-START
 *  - 3 events for MAPPING-START
 */

static int
yaml_emitter_need_more_events(yaml_emitter_t *emitter)
{
    int level = 0;
    int accumulate = 0;
    yaml_event_t *event;

    if (QUEUE_EMPTY(emitter, emitter->events))
        return 1;

    switch (emitter->events.head->type) {
        case YAML_DOCUMENT_START_EVENT:
            accumulate = 1;
            break;
        case YAML_SEQUENCE_START_EVENT:
            accumulate = 2;
            break;
        case YAML_MAPPING_START_EVENT:
            accumulate = 3;
            break;
        default:
            return 0;
    }

    if (emitter->events.tail - emitter->events.head > accumulate)
        return 0;

    for (event = emitter->events.head; event != emitter->events.tail; event ++) {
        switch (event->type) {
            case YAML_STREAM_START_EVENT:
            case YAML_DOCUMENT_START_EVENT:
            case YAML_SEQUENCE_START_EVENT:
            case YAML_MAPPING_START_EVENT:
                level += 1;
                break;
            case YAML_STREAM_END_EVENT:
            case YAML_DOCUMENT_END_EVENT:
            case YAML_SEQUENCE_END_EVENT:
            case YAML_MAPPING_END_EVENT:
                level -= 1;
                break;
            default:
                break;
        }
        if (!level)
            return 0;
    }

    return 1;
}

/*
 * Append a directive to the directives stack.
 */

static int
yaml_emitter_append_tag_directive(yaml_emitter_t *emitter,
        yaml_tag_directive_t value, int allow_duplicates)
{
    yaml_tag_directive_t *tag_directive;
    yaml_tag_directive_t copy = { NULL, NULL };

    for (tag_directive = emitter->tag_directives.start;
            tag_directive != emitter->tag_directives.top; tag_directive ++) {
        if (strcmp((char *)value.handle, (char *)tag_directive->handle) == 0) {
            if (allow_duplicates)
                return 1;
            return yaml_emitter_set_emitter_error(emitter,
                    "duplicate %TAG directive");
        }
    }

    copy.handle = yaml_strdup(value.handle);
    copy.prefix = yaml_strdup(value.prefix);
    if (!copy.handle || !copy.prefix) {
        emitter->error = YAML_MEMORY_ERROR;
        goto error;
    }

    if (!PUSH(emitter, emitter->tag_directives, copy))
        goto error;

    return 1;

error:
    yaml_free(copy.handle);
    yaml_free(copy.prefix);
    return 0;
}

/*
 * Increase the indentation level.
 */

static int
yaml_emitter_increase_indent(yaml_emitter_t *emitter,
        int flow, int indentless)
{
    if (!PUSH(emitter, emitter->indents, emitter->indent))
        return 0;

    if (emitter->indent < 0) {
        emitter->indent = flow ? emitter->best_indent : 0;
    }
    else if (!indentless) {
        emitter->indent += emitter->best_indent;
    }

    return 1;
}

/*
 * State dispatcher.
 */

static int
yaml_emitter_state_machine(yaml_emitter_t *emitter, yaml_event_t *event)
{
    switch (emitter->state)
    {
        case YAML_EMIT_STREAM_START_STATE:
            return yaml_emitter_emit_stream_start(emitter, event);

        case YAML_EMIT_FIRST_DOCUMENT_START_STATE:
            return yaml_emitter_emit_document_start(emitter, event, 1);

        case YAML_EMIT_DOCUMENT_START_STATE:
            return yaml_emitter_emit_document_start(emitter, event, 0);

        case YAML_EMIT_DOCUMENT_CONTENT_STATE:
            return yaml_emitter_emit_document_content(emitter, event);

        case YAML_EMIT_DOCUMENT_END_STATE:
            return yaml_emitter_emit_document_end(emitter, event);

        case YAML_EMIT_FLOW_SEQUENCE_FIRST_ITEM_STATE:
            return yaml_emitter_emit_flow_sequence_item(emitter, event, 1);

        case YAML_EMIT_FLOW_SEQUENCE_ITEM_STATE:
            return yaml_emitter_emit_flow_sequence_item(emitter, event, 0);

        case YAML_EMIT_FLOW_MAPPING_FIRST_KEY_STATE:
            return yaml_emitter_emit_flow_mapping_key(emitter, event, 1);

        case YAML_EMIT_FLOW_MAPPING_KEY_STATE:
            return yaml_emitter_emit_flow_mapping_key(emitter, event, 0);

        case YAML_EMIT_FLOW_MAPPING_SIMPLE_VALUE_STATE:
            return yaml_emitter_emit_flow_mapping_value(emitter, event, 1);

        case YAML_EMIT_FLOW_MAPPING_VALUE_STATE:
            return yaml_emitter_emit_flow_mapping_value(emitter, event, 0);

        case YAML_EMIT_BLOCK_SEQUENCE_FIRST_ITEM_STATE:
            return yaml_emitter_emit_block_sequence_item(emitter, event, 1);

        case YAML_EMIT_BLOCK_SEQUENCE_ITEM_STATE:
            return yaml_emitter_emit_block_sequence_item(emitter, event, 0);

        case YAML_EMIT_BLOCK_MAPPING_FIRST_KEY_STATE:
            return yaml_emitter_emit_block_mapping_key(emitter, event, 1);

        case YAML_EMIT_BLOCK_MAPPING_KEY_STATE:
            return yaml_emitter_emit_block_mapping_key(emitter, event, 0);

        case YAML_EMIT_BLOCK_MAPPING_SIMPLE_VALUE_STATE:
            return yaml_emitter_emit_block_mapping_value(emitter, event, 1);

        case YAML_EMIT_BLOCK_MAPPING_VALUE_STATE:
            return yaml_emitter_emit_block_mapping_value(emitter, event, 0);

        case YAML_EMIT_END_STATE:
            return yaml_emitter_set_emitter_error(emitter,
                    "expected nothing after STREAM-END");

        default:
            assert(1);      /* Invalid state. */
    }

    return 0;
}

/*
 * Expect STREAM-START.
 */

static int
yaml_emitter_emit_stream_start(yaml_emitter_t *emitter,
        yaml_event_t *event)
{
    emitter->open_ended = 0;
    if (event->type == YAML_STREAM_START_EVENT)
    {
        if (!emitter->encoding) {
            emitter->encoding = event->data.stream_start.encoding;
        }

        if (!emitter->encoding) {
            emitter->encoding = YAML_UTF8_ENCODING;
        }

        if (emitter->best_indent < 2 || emitter->best_indent > 9) {
            emitter->best_indent  = 2;
        }

        if (emitter->best_width >= 0
                && emitter->best_width <= emitter->best_indent*2) {
            emitter->best_width = 80;
        }

        if (emitter->best_width < 0) {
            emitter->best_width = INT_MAX;
        }

        if (!emitter->line_break) {
            emitter->line_break = YAML_LN_BREAK;
        }

        emitter->indent = -1;

        emitter->line = 0;
        emitter->column = 0;
        emitter->whitespace = 1;
        emitter->indention = 1;

        if (emitter->encoding != YAML_UTF8_ENCODING) {
            if (!yaml_emitter_write_bom(emitter))
                return 0;
        }

        emitter->state = YAML_EMIT_FIRST_DOCUMENT_START_STATE;

        return 1;
    }

    return yaml_emitter_set_emitter_error(emitter,
            "expected STREAM-START");
}

/*
 * Expect DOCUMENT-START or STREAM-END.
 */

static int
yaml_emitter_emit_document_start(yaml_emitter_t *emitter,
        yaml_event_t *event, int first)
{
    if (event->type == YAML_DOCUMENT_START_EVENT)
    {
        yaml_tag_directive_t default_tag_directives[] = {
            {(yaml_char_t *)"!", (yaml_char_t *)"!"},
            {(yaml_char_t *)"!!", (yaml_char_t *)"tag:yaml.org,2002:"},
            {NULL, NULL}
        };
        yaml_tag_directive_t *tag_directive;
        int implicit;

        if (event->data.document_start.version_directive) {
            if (!yaml_emitter_analyze_version_directive(emitter,
                        *event->data.document_start.version_directive))
                return 0;
        }

        for (tag_directive = event->data.document_start.tag_directives.start;
                tag_directive != event->data.document_start.tag_directives.end;
                tag_directive ++) {
            if (!yaml_emitter_analyze_tag_directive(emitter, *tag_directive))
                return 0;
            if (!yaml_emitter_append_tag_directive(emitter, *tag_directive, 0))
                return 0;
        }

        for (tag_directive = default_tag_directives;
                tag_directive->handle; tag_directive ++) {
            if (!yaml_emitter_append_tag_directive(emitter, *tag_directive, 1))
                return 0;
        }

        implicit = event->data.document_start.implicit;
        if (!first || emitter->canonical) {
            implicit = 0;
        }

        if ((event->data.document_start.version_directive ||
                    (event->data.document_start.tag_directives.start
                     != event->data.document_start.tag_directives.end)) &&
                emitter->open_ended)
        {
            if (!yaml_emitter_write_indicator(emitter, "...", 1, 0, 0))
                return 0;
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        emitter->open_ended = 0;

        if (event->data.document_start.version_directive) {
            implicit = 0;
            if (!yaml_emitter_write_indicator(emitter, "%YAML", 1, 0, 0))
                return 0;
            if (event->data.document_start.version_directive->minor == 1) {
                if (!yaml_emitter_write_indicator(emitter, "1.1", 1, 0, 0))
                    return 0;
            }
            else {
                if (!yaml_emitter_write_indicator(emitter, "1.2", 1, 0, 0))
                    return 0;
            }
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }

        if (event->data.document_start.tag_directives.start
                != event->data.document_start.tag_directives.end) {
            implicit = 0;
            for (tag_directive = event->data.document_start.tag_directives.start;
                    tag_directive != event->data.document_start.tag_directives.end;
                    tag_directive ++) {
                if (!yaml_emitter_write_indicator(emitter, "%TAG", 1, 0, 0))
                    return 0;
                if (!yaml_emitter_write_tag_handle(emitter, tag_directive->handle,
                            strlen((char *)tag_directive->handle)))
                    return 0;
                if (!yaml_emitter_write_tag_content(emitter, tag_directive->prefix,
                            strlen((char *)tag_directive->prefix), 1))
                    return 0;
                if (!yaml_emitter_write_indent(emitter))
                    return 0;
            }
        }

        if (yaml_emitter_check_empty_document(emitter)) {
            implicit = 0;
        }

        if (!implicit) {
            if (!yaml_emitter_write_indent(emitter))
                return 0;
            if (!yaml_emitter_write_indicator(emitter, "---", 1, 0, 0))
                return 0;
            if (emitter->canonical) {
                if (!yaml_emitter_write_indent(emitter))
                    return 0;
            }
        }

        emitter->state = YAML_EMIT_DOCUMENT_CONTENT_STATE;

        emitter->open_ended = 0;
        return 1;
    }

    else if (event->type == YAML_STREAM_END_EVENT)
    {

        /**
         * This can happen if a block scalar with trailing empty lines
         * is at the end of the stream
         */
        if (emitter->open_ended == 2)
        {
            if (!yaml_emitter_write_indicator(emitter, "...", 1, 0, 0))
                return 0;
            emitter->open_ended = 0;
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        if (!yaml_emitter_flush(emitter))
            return 0;

        emitter->state = YAML_EMIT_END_STATE;

        return 1;
    }

    return yaml_emitter_set_emitter_error(emitter,
            "expected DOCUMENT-START or STREAM-END");
}

/*
 * Expect the root node.
 */

static int
yaml_emitter_emit_document_content(yaml_emitter_t *emitter,
        yaml_event_t *event)
{
    if (!PUSH(emitter, emitter->states, YAML_EMIT_DOCUMENT_END_STATE))
        return 0;

    return yaml_emitter_emit_node(emitter, event, 1, 0, 0, 0);
}

/*
 * Expect DOCUMENT-END.
 */

static int
yaml_emitter_emit_document_end(yaml_emitter_t *emitter,
        yaml_event_t *event)
{
    if (event->type == YAML_DOCUMENT_END_EVENT)
    {
        if (!yaml_emitter_write_indent(emitter))
            return 0;
        if (!event->data.document_end.implicit) {
            if (!yaml_emitter_write_indicator(emitter, "...", 1, 0, 0))
                return 0;
            emitter->open_ended = 0;
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        else if (!emitter->open_ended)
            emitter->open_ended = 1;
        if (!yaml_emitter_flush(emitter))
            return 0;

        emitter->state = YAML_EMIT_DOCUMENT_START_STATE;

        while (!STACK_EMPTY(emitter, emitter->tag_directives)) {
            yaml_tag_directive_t tag_directive = POP(emitter,
                    emitter->tag_directives);
            yaml_free(tag_directive.handle);
            yaml_free(tag_directive.prefix);
        }

        return 1;
    }

    return yaml_emitter_set_emitter_error(emitter,
            "expected DOCUMENT-END");
}

/*
 *
 * Expect a flow item node.
 */

static int
yaml_emitter_emit_flow_sequence_item(yaml_emitter_t *emitter,
        yaml_event_t *event, int first)
{
    if (first)
    {
        if (!yaml_emitter_write_indicator(emitter, "[", 1, 1, 0))
            return 0;
        if (!yaml_emitter_increase_indent(emitter, 1, 0))
            return 0;
        emitter->flow_level ++;
    }

    if (event->type == YAML_SEQUENCE_END_EVENT)
    {
        emitter->flow_level --;
        emitter->indent = POP(emitter, emitter->indents);
        if (emitter->canonical && !first) {
            if (!yaml_emitter_write_indicator(emitter, ",", 0, 0, 0))
                return 0;
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        if (!yaml_emitter_write_indicator(emitter, "]", 0, 0, 0))
            return 0;
        emitter->state = POP(emitter, emitter->states);

        return 1;
    }

    if (!first) {
        if (!yaml_emitter_write_indicator(emitter, ",", 0, 0, 0))
            return 0;
    }

    if (emitter->canonical || emitter->column > emitter->best_width) {
        if (!yaml_emitter_write_indent(emitter))
            return 0;
    }
    if (!PUSH(emitter, emitter->states, YAML_EMIT_FLOW_SEQUENCE_ITEM_STATE))
        return 0;

    return yaml_emitter_emit_node(emitter, event, 0, 1, 0, 0);
}

/*
 * Expect a flow key node.
 */

static int
yaml_emitter_emit_flow_mapping_key(yaml_emitter_t *emitter,
        yaml_event_t *event, int first)
{
    if (first)
    {
        if (!yaml_emitter_write_indicator(emitter, "{", 1, 1, 0))
            return 0;
        if (!yaml_emitter_increase_indent(emitter, 1, 0))
            return 0;
        emitter->flow_level ++;
    }

    if (event->type == YAML_MAPPING_END_EVENT)
    {
        emitter->flow_level --;
        emitter->indent = POP(emitter, emitter->indents);
        if (emitter->canonical && !first) {
            if (!yaml_emitter_write_indicator(emitter, ",", 0, 0, 0))
                return 0;
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        if (!yaml_emitter_write_indicator(emitter, "}", 0, 0, 0))
            return 0;
        emitter->state = POP(emitter, emitter->states);

        return 1;
    }

    if (!first) {
        if (!yaml_emitter_write_indicator(emitter, ",", 0, 0, 0))
            return 0;
    }
    if (emitter->canonical || emitter->column > emitter->best_width) {
        if (!yaml_emitter_write_indent(emitter))
            return 0;
    }

    if (!emitter->canonical && yaml_emitter_check_simple_key(emitter))
    {
        if (!PUSH(emitter, emitter->states,
                    YAML_EMIT_FLOW_MAPPING_SIMPLE_VALUE_STATE))
            return 0;

        return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 1);
    }
    else
    {
        if (!yaml_emitter_write_indicator(emitter, "?", 1, 0, 0))
            return 0;
        if (!PUSH(emitter, emitter->states,
                    YAML_EMIT_FLOW_MAPPING_VALUE_STATE))
            return 0;

        return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 0);
    }
}

/*
 * Expect a flow value node.
 */

static int
yaml_emitter_emit_flow_mapping_value(yaml_emitter_t *emitter,
        yaml_event_t *event, int simple)
{
    if (simple) {
        if (!yaml_emitter_write_indicator(emitter, ":", 0, 0, 0))
            return 0;
    }
    else {
        if (emitter->canonical || emitter->column > emitter->best_width) {
            if (!yaml_emitter_write_indent(emitter))
                return 0;
        }
        if (!yaml_emitter_write_indicator(emitter, ":", 1, 0, 0))
            return 0;
    }
    if (!PUSH(emitter, emitter->states, YAML_EMIT_FLOW_MAPPING_KEY_STATE))
        return 0;
    return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 0);
}

/*
 * Expect a block item node.
 */

static int
yaml_emitter_emit_block_sequence_item(yaml_emitter_t *emitter,
        yaml_event_t *event, int first)
{
    if (first)
    {
        if (!yaml_emitter_increase_indent(emitter, 0,
                    (emitter->mapping_context && !emitter->indention)))
            return 0;
    }

    if (event->type == YAML_SEQUENCE_END_EVENT)
    {
        emitter->indent = POP(emitter, emitter->indents);
        emitter->state = POP(emitter, emitter->states);

        return 1;
    }

    if (!yaml_emitter_write_indent(emitter))
        return 0;
    if (!yaml_emitter_write_indicator(emitter, "-", 1, 0, 1))
        return 0;
    if (!PUSH(emitter, emitter->states,
                YAML_EMIT_BLOCK_SEQUENCE_ITEM_STATE))
        return 0;

    return yaml_emitter_emit_node(emitter, event, 0, 1, 0, 0);
}

/*
 * Expect a block key node.
 */

static int
yaml_emitter_emit_block_mapping_key(yaml_emitter_t *emitter,
        yaml_event_t *event, int first)
{
    if (first)
    {
        if (!yaml_emitter_increase_indent(emitter, 0, 0))
            return 0;
    }

    if (event->type == YAML_MAPPING_END_EVENT)
    {
        emitter->indent = POP(emitter, emitter->indents);
        emitter->state = POP(emitter, emitter->states);

        return 1;
    }

    if (!yaml_emitter_write_indent(emitter))
        return 0;

    if (yaml_emitter_check_simple_key(emitter))
    {
        if (!PUSH(emitter, emitter->states,
                    YAML_EMIT_BLOCK_MAPPING_SIMPLE_VALUE_STATE))
            return 0;

        return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 1);
    }
    else
    {
        if (!yaml_emitter_write_indicator(emitter, "?", 1, 0, 1))
            return 0;
        if (!PUSH(emitter, emitter->states,
                    YAML_EMIT_BLOCK_MAPPING_VALUE_STATE))
            return 0;

        return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 0);
    }
}

/*
 * Expect a block value node.
 */

static int
yaml_emitter_emit_block_mapping_value(yaml_emitter_t *emitter,
        yaml_event_t *event, int simple)
{
    if (simple) {
        if (!yaml_emitter_write_indicator(emitter, ":", 0, 0, 0))
            return 0;
    }
    else {
        if (!yaml_emitter_write_indent(emitter))
            return 0;
        if (!yaml_emitter_write_indicator(emitter, ":", 1, 0, 1))
            return 0;
    }
    if (!PUSH(emitter, emitter->states,
                YAML_EMIT_BLOCK_MAPPING_KEY_STATE))
        return 0;

    return yaml_emitter_emit_node(emitter, event, 0, 0, 1, 0);
}

/*
 * Expect a node.
 */

static int
yaml_emitter_emit_node(yaml_emitter_t *emitter, yaml_event_t *event,
        int root, int sequence, int mapping, int simple_key)
{
    emitter->root_context = root;
    emitter->sequence_context = sequence;
    emitter->mapping_context = mapping;
    emitter->simple_key_context = simple_key;

    switch (event->type)
    {
        case YAML_ALIAS_EVENT:
            return yaml_emitter_emit_alias(emitter, event);

        case YAML_SCALAR_EVENT:
            return yaml_emitter_emit_scalar(emitter, event);

        case YAML_SEQUENCE_START_EVENT:
            return yaml_emitter_emit_sequence_start(emitter, event);

        case YAML_MAPPING_START_EVENT:
            return yaml_emitter_emit_mapping_start(emitter, event);

        default:
            return yaml_emitter_set_emitter_error(emitter,
                    "expected SCALAR, SEQUENCE-START, MAPPING-START, or ALIAS");
    }

    return 0;
}

/*
 * Expect ALIAS.
 */

static int
yaml_emitter_emit_alias(yaml_emitter_t *emitter, SHIM(yaml_event_t *event))
{
    if (!yaml_emitter_process_anchor(emitter))
        return 0;
    if (emitter->simple_key_context)
        if (!PUT(emitter, ' ')) return 0;
    emitter->state = POP(emitter, emitter->states);

    return 1;
}

/*
 * Expect SCALAR.
 */

static int
yaml_emitter_emit_scalar(yaml_emitter_t *emitter, yaml_event_t *event)
{
    if (!yaml_emitter_select_scalar_style(emitter, event))
        return 0;
    if (!yaml_emitter_process_anchor(emitter))
        return 0;
    if (!yaml_emitter_process_tag(emitter))
        return 0;
    if (!yaml_emitter_increase_indent(emitter, 1, 0))
        return 0;
    if (!yaml_emitter_process_scalar(emitter))
        return 0;
    emitter->indent = POP(emitter, emitter->indents);
    emitter->state = POP(emitter, emitter->states);

    return 1;
}

/*
 * Expect SEQUENCE-START.
 */

static int
yaml_emitter_emit_sequence_start(yaml_emitter_t *emitter, yaml_event_t *event)
{
    if (!yaml_emitter_process_anchor(emitter))
        return 0;
    if (!yaml_emitter_process_tag(emitter))
        return 0;

    if (emitter->flow_level || emitter->canonical
            || event->data.sequence_start.style == YAML_FLOW_SEQUENCE_STYLE
            || yaml_emitter_check_empty_sequence(emitter)) {
        emitter->state = YAML_EMIT_FLOW_SEQUENCE_FIRST_ITEM_STATE;
    }
    else {
        emitter->state = YAML_EMIT_BLOCK_SEQUENCE_FIRST_ITEM_STATE;
    }

    return 1;
}

/*
 * Expect MAPPING-START.
 */

static int
yaml_emitter_emit_mapping_start(yaml_emitter_t *emitter, yaml_event_t *event)
{
    if (!yaml_emitter_process_anchor(emitter))
        return 0;
    if (!yaml_emitter_process_tag(emitter))
        return 0;

    if (emitter->flow_level || emitter->canonical
            || event->data.mapping_start.style == YAML_FLOW_MAPPING_STYLE
            || yaml_emitter_check_empty_mapping(emitter)) {
        emitter->state = YAML_EMIT_FLOW_MAPPING_FIRST_KEY_STATE;
    }
    else {
        emitter->state = YAML_EMIT_BLOCK_MAPPING_FIRST_KEY_STATE;
    }

    return 1;
}

/*
 * Check if the document content is an empty scalar.
 */

static int
yaml_emitter_check_empty_document(SHIM(yaml_emitter_t *emitter))
{
    return 0;
}

/*
 * Check if the next events represent an empty sequence.
 */

static int
yaml_emitter_check_empty_sequence(yaml_emitter_t *emitter)
{
    if (emitter->events.tail - emitter->events.head < 2)
        return 0;

    return (emitter->events.head[0].type == YAML_SEQUENCE_START_EVENT
            && emitter->events.head[1].type == YAML_SEQUENCE_END_EVENT);
}

/*
 * Check if the next events represent an empty mapping.
 */

static int
yaml_emitter_check_empty_mapping(yaml_emitter_t *emitter)
{
    if (emitter->events.tail - emitter->events.head < 2)
        return 0;

    return (emitter->events.head[0].type == YAML_MAPPING_START_EVENT
            && emitter->events.head[1].type == YAML_MAPPING_END_EVENT);
}

/*
 * Check if the next node can be expressed as a simple key.
 */

static int
yaml_emitter_check_simple_key(yaml_emitter_t *emitter)
{
    yaml_event_t *event = emitter->events.head;
    size_t length = 0;

    switch (event->type)
    {
        case YAML_ALIAS_EVENT:
            length += emitter->anchor_data.anchor_length;
            break;

        case YAML_SCALAR_EVENT:
            if (emitter->scalar_data.multiline)
                return 0;
            length += emitter->anchor_data.anchor_length
                + emitter->tag_data.handle_length
                + emitter->tag_data.suffix_length
                + emitter->scalar_data.length;
            break;

        case YAML_SEQUENCE_START_EVENT:
            if (!yaml_emitter_check_empty_sequence(emitter))
                return 0;
            length += emitter->anchor_data.anchor_length
                + emitter->tag_data.handle_length
                + emitter->tag_data.suffix_length;
            break;

        case YAML_MAPPING_START_EVENT:
            if (!yaml_emitter_check_empty_mapping(emitter))
                return 0;
            length += emitter->anchor_data.anchor_length
                + emitter->tag_data.handle_length
                + emitter->tag_data.suffix_length;
            break;

        default:
            return 0;
    }

    if (length > 128)
        return 0;

    return 1;
}

/*
 * Determine an acceptable scalar style.
 */

static int
yaml_emitter_select_scalar_style(yaml_emitter_t *emitter, yaml_event_t *event)
{
    yaml_scalar_style_t style = event->data.scalar.style;
    int no_tag = (!emitter->tag_data.handle && !emitter->tag_data.suffix);

    if (no_tag && !event->data.scalar.plain_implicit
            && !event->data.scalar.quoted_implicit) {
        return yaml_emitter_set_emitter_error(emitter,
                "neither tag nor implicit flags are specified");
    }

    if (style == YAML_ANY_SCALAR_STYLE)
        style = YAML_PLAIN_SCALAR_STYLE;

    if (emitter->canonical)
        style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;

    if (emitter->simple_key_context && emitter->scalar_data.multiline)
        style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;

    if (style == YAML_PLAIN_SCALAR_STYLE)
    {
        if ((emitter->flow_level && !emitter->scalar_data.flow_plain_allowed)
                || (!emitter->flow_level && !emitter->scalar_data.block_plain_allowed))
            style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
        if (!emitter->scalar_data.length
                && (emitter->flow_level || emitter->simple_key_context))
            style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
        if (no_tag && !event->data.scalar.plain_implicit)
            style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
    }

    if (style == YAML_SINGLE_QUOTED_SCALAR_STYLE)
    {
        if (!emitter->scalar_data.single_quoted_allowed)
            style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }

    if (style == YAML_LITERAL_SCALAR_STYLE || style == YAML_FOLDED_SCALAR_STYLE)
    {
        if (!emitter->scalar_data.block_allowed
                || emitter->flow_level || emitter->simple_key_context)
            style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }

    if (no_tag && !event->data.scalar.quoted_implicit
            && style != YAML_PLAIN_SCALAR_STYLE)
    {
        emitter->tag_data.handle = (yaml_char_t *)"!";
        emitter->tag_data.handle_length = 1;
    }

    emitter->scalar_data.style = style;

    return 1;
}

/*
 * Write an anchor.
 */

static int
yaml_emitter_process_anchor(yaml_emitter_t *emitter)
{
    if (!emitter->anchor_data.anchor)
        return 1;

    if (!yaml_emitter_write_indicator(emitter,
                (emitter->anchor_data.alias ? "*" : "&"), 1, 0, 0))
        return 0;

    return yaml_emitter_write_anchor(emitter,
            emitter->anchor_data.anchor, emitter->anchor_data.anchor_length);
}

/*
 * Write a tag.
 */

static int
yaml_emitter_process_tag(yaml_emitter_t *emitter)
{
    if (!emitter->tag_data.handle && !emitter->tag_data.suffix)
        return 1;

    if (emitter->tag_data.handle)
    {
        if (!yaml_emitter_write_tag_handle(emitter, emitter->tag_data.handle,
                    emitter->tag_data.handle_length))
            return 0;
        if (emitter->tag_data.suffix) {
            if (!yaml_emitter_write_tag_content(emitter, emitter->tag_data.suffix,
                        emitter->tag_data.suffix_length, 0))
                return 0;
        }
    }
    else
    {
        if (!yaml_emitter_write_indicator(emitter, "!<", 1, 0, 0))
            return 0;
        if (!yaml_emitter_write_tag_content(emitter, emitter->tag_data.suffix,
                    emitter->tag_data.suffix_length, 0))
            return 0;
        if (!yaml_emitter_write_indicator(emitter, ">", 0, 0, 0))
            return 0;
    }

    return 1;
}

/*
 * Write a scalar.
 */

static int
yaml_emitter_process_scalar(yaml_emitter_t *emitter)
{
    switch (emitter->scalar_data.style)
    {
        case YAML_PLAIN_SCALAR_STYLE:
            return yaml_emitter_write_plain_scalar(emitter,
                    emitter->scalar_data.value, emitter->scalar_data.length,
                    !emitter->simple_key_context);

        case YAML_SINGLE_QUOTED_SCALAR_STYLE:
            return yaml_emitter_write_single_quoted_scalar(emitter,
                    emitter->scalar_data.value, emitter->scalar_data.length,
                    !emitter->simple_key_context);

        case YAML_DOUBLE_QUOTED_SCALAR_STYLE:
            return yaml_emitter_write_double_quoted_scalar(emitter,
                    emitter->scalar_data.value, emitter->scalar_data.length,
                    !emitter->simple_key_context);

        case YAML_LITERAL_SCALAR_STYLE:
            return yaml_emitter_write_literal_scalar(emitter,
                    emitter->scalar_data.value, emitter->scalar_data.length);

        case YAML_FOLDED_SCALAR_STYLE:
            return yaml_emitter_write_folded_scalar(emitter,
                    emitter->scalar_data.value, emitter->scalar_data.length);

        default:
            assert(1);      /* Impossible. */
    }

    return 0;
}

/*
 * Check if a %YAML directive is valid.
 */

static int
yaml_emitter_analyze_version_directive(yaml_emitter_t *emitter,
        yaml_version_directive_t version_directive)
{
    if (version_directive.major != 1 || (
        version_directive.minor != 1
        && version_directive.minor != 2
        )) {
        return yaml_emitter_set_emitter_error(emitter,
                "incompatible %YAML directive");
    }

    return 1;
}

/*
 * Check if a %TAG directive is valid.
 */

static int
yaml_emitter_analyze_tag_directive(yaml_emitter_t *emitter,
        yaml_tag_directive_t tag_directive)
{
    yaml_string_t handle;
    yaml_string_t prefix;
    size_t handle_length;
    size_t prefix_length;

    handle_length = strlen((char *)tag_directive.handle);
    prefix_length = strlen((char *)tag_directive.prefix);
    STRING_ASSIGN(handle, tag_directive.handle, handle_length);
    STRING_ASSIGN(prefix, tag_directive.prefix, prefix_length);

    if (handle.start == handle.end) {
        return yaml_emitter_set_emitter_error(emitter,
                "tag handle must not be empty");
    }

    if (handle.start[0] != '!') {
        return yaml_emitter_set_emitter_error(emitter,
                "tag handle must start with '!'");
    }

    if (handle.end[-1] != '!') {
        return yaml_emitter_set_emitter_error(emitter,
                "tag handle must end with '!'");
    }

    handle.pointer ++;

    while (handle.pointer < handle.end-1) {
        if (!IS_ALPHA(handle)) {
            return yaml_emitter_set_emitter_error(emitter,
                    "tag handle must contain alphanumerical characters only");
        }
        MOVE(handle);
    }

    if (prefix.start == prefix.end) {
        return yaml_emitter_set_emitter_error(emitter,
                "tag prefix must not be empty");
    }

    return 1;
}

/*
 * Check if an anchor is valid.
 */

static int
yaml_emitter_analyze_anchor(yaml_emitter_t *emitter,
        yaml_char_t *anchor, int alias)
{
    size_t anchor_length;
    yaml_string_t string;

    anchor_length = strlen((char *)anchor);
    STRING_ASSIGN(string, anchor, anchor_length);

    if (string.start == string.end) {
        return yaml_emitter_set_emitter_error(emitter, alias ?
                "alias value must not be empty" :
                "anchor value must not be empty");
    }

    while (string.pointer != string.end) {
        if (!IS_ALPHA(string)) {
            return yaml_emitter_set_emitter_error(emitter, alias ?
                    "alias value must contain alphanumerical characters only" :
                    "anchor value must contain alphanumerical characters only");
        }
        MOVE(string);
    }

    emitter->anchor_data.anchor = string.start;
    emitter->anchor_data.anchor_length = string.end - string.start;
    emitter->anchor_data.alias = alias;

    return 1;
}

/*
 * Check if a tag is valid.
 */

static int
yaml_emitter_analyze_tag(yaml_emitter_t *emitter,
        yaml_char_t *tag)
{
    size_t tag_length;
    yaml_string_t string;
    yaml_tag_directive_t *tag_directive;

    tag_length = strlen((char *)tag);
    STRING_ASSIGN(string, tag, tag_length);

    if (string.start == string.end) {
        return yaml_emitter_set_emitter_error(emitter,
                "tag value must not be empty");
    }

    for (tag_directive = emitter->tag_directives.start;
            tag_directive != emitter->tag_directives.top; tag_directive ++) {
        size_t prefix_length = strlen((char *)tag_directive->prefix);
        if (prefix_length < (size_t)(string.end - string.start)
                && strncmp((char *)tag_directive->prefix, (char *)string.start,
                    prefix_length) == 0)
        {
            emitter->tag_data.handle = tag_directive->handle;
            emitter->tag_data.handle_length =
                strlen((char *)tag_directive->handle);
            emitter->tag_data.suffix = string.start + prefix_length;
            emitter->tag_data.suffix_length =
                (string.end - string.start) - prefix_length;
            return 1;
        }
    }

    emitter->tag_data.suffix = string.start;
    emitter->tag_data.suffix_length = string.end - string.start;

    return 1;
}

/*
 * Check if a scalar is valid.
 */

static int
yaml_emitter_analyze_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length)
{
    yaml_string_t string;

    int block_indicators = 0;
    int flow_indicators = 0;
    int line_breaks = 0;
    int special_characters = 0;

    int leading_space = 0;
    int leading_break = 0;
    int trailing_space = 0;
    int trailing_break = 0;
    int break_space = 0;
    int space_break = 0;

    int preceded_by_whitespace = 0;
    int followed_by_whitespace = 0;
    int previous_space = 0;
    int previous_break = 0;

    STRING_ASSIGN(string, value, length);

    emitter->scalar_data.value = value;
    emitter->scalar_data.length = length;

    if (string.start == string.end)
    {
        emitter->scalar_data.multiline = 0;
        emitter->scalar_data.flow_plain_allowed = 0;
        emitter->scalar_data.block_plain_allowed = 1;
        emitter->scalar_data.single_quoted_allowed = 1;
        emitter->scalar_data.block_allowed = 0;

        return 1;
    }

    if ((CHECK_AT(string, '-', 0)
                && CHECK_AT(string, '-', 1)
                && CHECK_AT(string, '-', 2))
            || (CHECK_AT(string, '.', 0)
                && CHECK_AT(string, '.', 1)
                && CHECK_AT(string, '.', 2))) {
        block_indicators = 1;
        flow_indicators = 1;
    }

    preceded_by_whitespace = 1;
    followed_by_whitespace = IS_BLANKZ_AT(string, WIDTH(string));

    while (string.pointer != string.end)
    {
        if (string.start == string.pointer)
        {
            if (CHECK(string, '#') || CHECK(string, ',')
                    || CHECK(string, '[') || CHECK(string, ']')
                    || CHECK(string, '{') || CHECK(string, '}')
                    || CHECK(string, '&') || CHECK(string, '*')
                    || CHECK(string, '!') || CHECK(string, '|')
                    || CHECK(string, '>') || CHECK(string, '\'')
                    || CHECK(string, '"') || CHECK(string, '%')
                    || CHECK(string, '@') || CHECK(string, '`')) {
                flow_indicators = 1;
                block_indicators = 1;
            }

            if (CHECK(string, '?') || CHECK(string, ':')) {
                flow_indicators = 1;
                if (followed_by_whitespace) {
                    block_indicators = 1;
                }
            }

            if (CHECK(string, '-') && followed_by_whitespace) {
                flow_indicators = 1;
                block_indicators = 1;
            }
        }
        else
        {
            if (CHECK(string, ',') || CHECK(string, '?')
                    || CHECK(string, '[') || CHECK(string, ']')
                    || CHECK(string, '{') || CHECK(string, '}')) {
                flow_indicators = 1;
            }

            if (CHECK(string, ':')) {
                flow_indicators = 1;
                if (followed_by_whitespace) {
                    block_indicators = 1;
                }
            }

            if (CHECK(string, '#') && preceded_by_whitespace) {
                flow_indicators = 1;
                block_indicators = 1;
            }
        }

        if (!IS_PRINTABLE(string)
                || (!IS_ASCII(string) && !emitter->unicode)) {
            special_characters = 1;
        }

        if (IS_BREAK(string)) {
            line_breaks = 1;
        }

        if (IS_SPACE(string))
        {
            if (string.start == string.pointer) {
                leading_space = 1;
            }
            if (string.pointer+WIDTH(string) == string.end) {
                trailing_space = 1;
            }
            if (previous_break) {
                break_space = 1;
            }
            previous_space = 1;
            previous_break = 0;
        }
        else if (IS_BREAK(string))
        {
            if (string.start == string.pointer) {
                leading_break = 1;
            }
            if (string.pointer+WIDTH(string) == string.end) {
                trailing_break = 1;
            }
            if (previous_space) {
                space_break = 1;
            }
            previous_space = 0;
            previous_break = 1;
        }
        else
        {
            previous_space = 0;
            previous_break = 0;
        }

        preceded_by_whitespace = IS_BLANKZ(string);
        MOVE(string);
        if (string.pointer != string.end) {
            followed_by_whitespace = IS_BLANKZ_AT(string, WIDTH(string));
        }
    }

    emitter->scalar_data.multiline = line_breaks;

    emitter->scalar_data.flow_plain_allowed = 1;
    emitter->scalar_data.block_plain_allowed = 1;
    emitter->scalar_data.single_quoted_allowed = 1;
    emitter->scalar_data.block_allowed = 1;

    if (leading_space || leading_break || trailing_space || trailing_break) {
        emitter->scalar_data.flow_plain_allowed = 0;
        emitter->scalar_data.block_plain_allowed = 0;
    }

    if (trailing_space) {
        emitter->scalar_data.block_allowed = 0;
    }

    if (break_space) {
        emitter->scalar_data.flow_plain_allowed = 0;
        emitter->scalar_data.block_plain_allowed = 0;
        emitter->scalar_data.single_quoted_allowed = 0;
    }

    if (space_break || special_characters) {
        emitter->scalar_data.flow_plain_allowed = 0;
        emitter->scalar_data.block_plain_allowed = 0;
        emitter->scalar_data.single_quoted_allowed = 0;
        emitter->scalar_data.block_allowed = 0;
    }

    if (line_breaks) {
        emitter->scalar_data.flow_plain_allowed = 0;
        emitter->scalar_data.block_plain_allowed = 0;
    }

    if (flow_indicators) {
        emitter->scalar_data.flow_plain_allowed = 0;
    }

    if (block_indicators) {
        emitter->scalar_data.block_plain_allowed = 0;
    }

    return 1;
}

/*
 * Check if the event data is valid.
 */

static int
yaml_emitter_analyze_event(yaml_emitter_t *emitter,
        yaml_event_t *event)
{
    emitter->anchor_data.anchor = NULL;
    emitter->anchor_data.anchor_length = 0;
    emitter->tag_data.handle = NULL;
    emitter->tag_data.handle_length = 0;
    emitter->tag_data.suffix = NULL;
    emitter->tag_data.suffix_length = 0;
    emitter->scalar_data.value = NULL;
    emitter->scalar_data.length = 0;

    switch (event->type)
    {
        case YAML_ALIAS_EVENT:
            if (!yaml_emitter_analyze_anchor(emitter,
                        event->data.alias.anchor, 1))
                return 0;
            return 1;

        case YAML_SCALAR_EVENT:
            if (event->data.scalar.anchor) {
                if (!yaml_emitter_analyze_anchor(emitter,
                            event->data.scalar.anchor, 0))
                    return 0;
            }
            if (event->data.scalar.tag && (emitter->canonical ||
                        (!event->data.scalar.plain_implicit
                         && !event->data.scalar.quoted_implicit))) {
                if (!yaml_emitter_analyze_tag(emitter, event->data.scalar.tag))
                    return 0;
            }
            if (!yaml_emitter_analyze_scalar(emitter,
                        event->data.scalar.value, event->data.scalar.length))
                return 0;
            return 1;

        case YAML_SEQUENCE_START_EVENT:
            if (event->data.sequence_start.anchor) {
                if (!yaml_emitter_analyze_anchor(emitter,
                            event->data.sequence_start.anchor, 0))
                    return 0;
            }
            if (event->data.sequence_start.tag && (emitter->canonical ||
                        !event->data.sequence_start.implicit)) {
                if (!yaml_emitter_analyze_tag(emitter,
                            event->data.sequence_start.tag))
                    return 0;
            }
            return 1;

        case YAML_MAPPING_START_EVENT:
            if (event->data.mapping_start.anchor) {
                if (!yaml_emitter_analyze_anchor(emitter,
                            event->data.mapping_start.anchor, 0))
                    return 0;
            }
            if (event->data.mapping_start.tag && (emitter->canonical ||
                        !event->data.mapping_start.implicit)) {
                if (!yaml_emitter_analyze_tag(emitter,
                            event->data.mapping_start.tag))
                    return 0;
            }
            return 1;

        default:
            return 1;
    }
}

/*
 * Write the BOM character.
 */

static int
yaml_emitter_write_bom(yaml_emitter_t *emitter)
{
    if (!FLUSH(emitter)) return 0;

    *(emitter->buffer.pointer++) = (yaml_char_t) '\xEF';
    *(emitter->buffer.pointer++) = (yaml_char_t) '\xBB';
    *(emitter->buffer.pointer++) = (yaml_char_t) '\xBF';

    return 1;
}

static int
yaml_emitter_write_indent(yaml_emitter_t *emitter)
{
    int indent = (emitter->indent >= 0) ? emitter->indent : 0;

    if (!emitter->indention || emitter->column > indent
            || (emitter->column == indent && !emitter->whitespace)) {
        if (!PUT_BREAK(emitter)) return 0;
    }

    while (emitter->column < indent) {
        if (!PUT(emitter, ' ')) return 0;
    }

    emitter->whitespace = 1;
    emitter->indention = 1;

    return 1;
}

static int
yaml_emitter_write_indicator(yaml_emitter_t *emitter,
        const char *indicator, int need_whitespace,
        int is_whitespace, int is_indention)
{
    size_t indicator_length;
    yaml_string_t string;

    indicator_length = strlen(indicator);
    STRING_ASSIGN(string, (yaml_char_t *)indicator, indicator_length);

    if (need_whitespace && !emitter->whitespace) {
        if (!PUT(emitter, ' ')) return 0;
    }

    while (string.pointer != string.end) {
        if (!WRITE(emitter, string)) return 0;
    }

    emitter->whitespace = is_whitespace;
    emitter->indention = (emitter->indention && is_indention);

    return 1;
}

static int
yaml_emitter_write_anchor(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length)
{
    yaml_string_t string;
    STRING_ASSIGN(string, value, length);

    while (string.pointer != string.end) {
        if (!WRITE(emitter, string)) return 0;
    }

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_tag_handle(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length)
{
    yaml_string_t string;
    STRING_ASSIGN(string, value, length);

    if (!emitter->whitespace) {
        if (!PUT(emitter, ' ')) return 0;
    }

    while (string.pointer != string.end) {
        if (!WRITE(emitter, string)) return 0;
    }

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_tag_content(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length,
        int need_whitespace)
{
    yaml_string_t string;
    STRING_ASSIGN(string, value, length);

    if (need_whitespace && !emitter->whitespace) {
        if (!PUT(emitter, ' ')) return 0;
    }

    while (string.pointer != string.end) {
        if (IS_ALPHA(string)
                || CHECK(string, ';') || CHECK(string, '/')
                || CHECK(string, '?') || CHECK(string, ':')
                || CHECK(string, '@') || CHECK(string, '&')
                || CHECK(string, '=') || CHECK(string, '+')
                || CHECK(string, '$') || CHECK(string, ',')
                || CHECK(string, '_') || CHECK(string, '.')
                || CHECK(string, '~') || CHECK(string, '*')
                || CHECK(string, '\'') || CHECK(string, '(')
                || CHECK(string, ')') || CHECK(string, '[')
                || CHECK(string, ']')) {
            if (!WRITE(emitter, string)) return 0;
        }
        else {
            int width = WIDTH(string);
            unsigned int value;
            while (width --) {
                value = *(string.pointer++);
                if (!PUT(emitter, '%')) return 0;
                if (!PUT(emitter, (value >> 4)
                            + ((value >> 4) < 10 ? '0' : 'A' - 10)))
                    return 0;
                if (!PUT(emitter, (value & 0x0F)
                            + ((value & 0x0F) < 10 ? '0' : 'A' - 10)))
                    return 0;
            }
        }
    }

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_plain_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks)
{
    yaml_string_t string;
    int spaces = 0;
    int breaks = 0;

    STRING_ASSIGN(string, value, length);

    /**
     * Avoid trailing spaces for empty values in block mode.
     * In flow mode, we still want the space to prevent ambiguous things
     * like {a:}.
     * Currently, the emitter forbids any plain empty scalar in flow mode
     * (e.g. it outputs {a: ''} instead), so emitter->flow_level will
     * never be true here.
     * But if the emitter is ever changed to allow emitting empty values,
     * the check for flow_level is already here.
     */
    if (!emitter->whitespace && (length || emitter->flow_level)) {
        if (!PUT(emitter, ' ')) return 0;
    }

    while (string.pointer != string.end)
    {
        if (IS_SPACE(string))
        {
            if (allow_breaks && !spaces
                    && emitter->column > emitter->best_width
                    && !IS_SPACE_AT(string, 1)) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
                MOVE(string);
            }
            else {
                if (!WRITE(emitter, string)) return 0;
            }
            spaces = 1;
        }
        else if (IS_BREAK(string))
        {
            if (!breaks && CHECK(string, '\n')) {
                if (!PUT_BREAK(emitter)) return 0;
            }
            if (!WRITE_BREAK(emitter, string)) return 0;
            emitter->indention = 1;
            breaks = 1;
        }
        else
        {
            if (breaks) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
            }
            if (!WRITE(emitter, string)) return 0;
            emitter->indention = 0;
            spaces = 0;
            breaks = 0;
        }
    }

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_single_quoted_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks)
{
    yaml_string_t string;
    int spaces = 0;
    int breaks = 0;

    STRING_ASSIGN(string, value, length);

    if (!yaml_emitter_write_indicator(emitter, "'", 1, 0, 0))
        return 0;

    while (string.pointer != string.end)
    {
        if (IS_SPACE(string))
        {
            if (allow_breaks && !spaces
                    && emitter->column > emitter->best_width
                    && string.pointer != string.start
                    && string.pointer != string.end - 1
                    && !IS_SPACE_AT(string, 1)) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
                MOVE(string);
            }
            else {
                if (!WRITE(emitter, string)) return 0;
            }
            spaces = 1;
        }
        else if (IS_BREAK(string))
        {
            if (!breaks && CHECK(string, '\n')) {
                if (!PUT_BREAK(emitter)) return 0;
            }
            if (!WRITE_BREAK(emitter, string)) return 0;
            emitter->indention = 1;
            breaks = 1;
        }
        else
        {
            if (breaks) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
            }
            if (CHECK(string, '\'')) {
                if (!PUT(emitter, '\'')) return 0;
            }
            if (!WRITE(emitter, string)) return 0;
            emitter->indention = 0;
            spaces = 0;
            breaks = 0;
        }
    }

    if (breaks)
        if (!yaml_emitter_write_indent(emitter)) return 0;

    if (!yaml_emitter_write_indicator(emitter, "'", 0, 0, 0))
        return 0;

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_double_quoted_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length, int allow_breaks)
{
    yaml_string_t string;
    int spaces = 0;

    STRING_ASSIGN(string, value, length);

    if (!yaml_emitter_write_indicator(emitter, "\"", 1, 0, 0))
        return 0;

    while (string.pointer != string.end)
    {
        if (!IS_PRINTABLE(string) || (!emitter->unicode && !IS_ASCII(string))
                || IS_BOM(string) || IS_BREAK(string)
                || CHECK(string, '"') || CHECK(string, '\\'))
        {
            unsigned char octet;
            unsigned int width;
            unsigned int value;
            int k;

            octet = string.pointer[0];
            width = (octet & 0x80) == 0x00 ? 1 :
                    (octet & 0xE0) == 0xC0 ? 2 :
                    (octet & 0xF0) == 0xE0 ? 3 :
                    (octet & 0xF8) == 0xF0 ? 4 : 0;
            value = (octet & 0x80) == 0x00 ? octet & 0x7F :
                    (octet & 0xE0) == 0xC0 ? octet & 0x1F :
                    (octet & 0xF0) == 0xE0 ? octet & 0x0F :
                    (octet & 0xF8) == 0xF0 ? octet & 0x07 : 0;
            for (k = 1; k < (int)width; k ++) {
                octet = string.pointer[k];
                value = (value << 6) + (octet & 0x3F);
            }
            string.pointer += width;

            if (!PUT(emitter, '\\')) return 0;

            switch (value)
            {
                case 0x00:
                    if (!PUT(emitter, '0')) return 0;
                    break;

                case 0x07:
                    if (!PUT(emitter, 'a')) return 0;
                    break;

                case 0x08:
                    if (!PUT(emitter, 'b')) return 0;
                    break;

                case 0x09:
                    if (!PUT(emitter, 't')) return 0;
                    break;

                case 0x0A:
                    if (!PUT(emitter, 'n')) return 0;
                    break;

                case 0x0B:
                    if (!PUT(emitter, 'v')) return 0;
                    break;

                case 0x0C:
                    if (!PUT(emitter, 'f')) return 0;
                    break;

                case 0x0D:
                    if (!PUT(emitter, 'r')) return 0;
                    break;

                case 0x1B:
                    if (!PUT(emitter, 'e')) return 0;
                    break;

                case 0x22:
                    if (!PUT(emitter, '\"')) return 0;
                    break;

                case 0x5C:
                    if (!PUT(emitter, '\\')) return 0;
                    break;

                case 0x85:
                    if (!PUT(emitter, 'N')) return 0;
                    break;

                case 0xA0:
                    if (!PUT(emitter, '_')) return 0;
                    break;

                case 0x2028:
                    if (!PUT(emitter, 'L')) return 0;
                    break;

                case 0x2029:
                    if (!PUT(emitter, 'P')) return 0;
                    break;

                default:
                    if (value <= 0xFF) {
                        if (!PUT(emitter, 'x')) return 0;
                        width = 2;
                    }
                    else if (value <= 0xFFFF) {
                        if (!PUT(emitter, 'u')) return 0;
                        width = 4;
                    }
                    else {
                        if (!PUT(emitter, 'U')) return 0;
                        width = 8;
                    }
                    for (k = (width-1)*4; k >= 0; k -= 4) {
                        int digit = (value >> k) & 0x0F;
                        if (!PUT(emitter, digit + (digit < 10 ? '0' : 'A'-10)))
                            return 0;
                    }
            }
            spaces = 0;
        }
        else if (IS_SPACE(string))
        {
            if (allow_breaks && !spaces
                    && emitter->column > emitter->best_width
                    && string.pointer != string.start
                    && string.pointer != string.end - 1) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
                if (IS_SPACE_AT(string, 1)) {
                    if (!PUT(emitter, '\\')) return 0;
                }
                MOVE(string);
            }
            else {
                if (!WRITE(emitter, string)) return 0;
            }
            spaces = 1;
        }
        else
        {
            if (!WRITE(emitter, string)) return 0;
            spaces = 0;
        }
    }

    if (!yaml_emitter_write_indicator(emitter, "\"", 0, 0, 0))
        return 0;

    emitter->whitespace = 0;
    emitter->indention = 0;

    return 1;
}

static int
yaml_emitter_write_block_scalar_hints(yaml_emitter_t *emitter,
        yaml_string_t string)
{
    char indent_hint[2];
    const char *chomp_hint = NULL;

    if (IS_SPACE(string) || IS_BREAK(string))
    {
        indent_hint[0] = '0' + (char)emitter->best_indent;
        indent_hint[1] = '\0';
        if (!yaml_emitter_write_indicator(emitter, indent_hint, 0, 0, 0))
            return 0;
    }

    emitter->open_ended = 0;

    string.pointer = string.end;
    if (string.start == string.pointer)
    {
        chomp_hint = "-";
    }
    else
    {
        do {
            string.pointer --;
        } while ((*string.pointer & 0xC0) == 0x80);
        if (!IS_BREAK(string))
        {
            chomp_hint = "-";
        }
        else if (string.start == string.pointer)
        {
            chomp_hint = "+";
            emitter->open_ended = 2;
        }
        else
        {
            do {
                string.pointer --;
            } while ((*string.pointer & 0xC0) == 0x80);
            if (IS_BREAK(string))
            {
                chomp_hint = "+";
                emitter->open_ended = 2;
            }
        }
    }

    if (chomp_hint)
    {
        if (!yaml_emitter_write_indicator(emitter, chomp_hint, 0, 0, 0))
            return 0;
    }

    return 1;
}

static int
yaml_emitter_write_literal_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length)
{
    yaml_string_t string;
    int breaks = 1;

    STRING_ASSIGN(string, value, length);

    if (!yaml_emitter_write_indicator(emitter, "|", 1, 0, 0))
        return 0;
    if (!yaml_emitter_write_block_scalar_hints(emitter, string))
        return 0;
    if (!PUT_BREAK(emitter)) return 0;
    emitter->indention = 1;
    emitter->whitespace = 1;

    while (string.pointer != string.end)
    {
        if (IS_BREAK(string))
        {
            if (!WRITE_BREAK(emitter, string)) return 0;
            emitter->indention = 1;
            breaks = 1;
        }
        else
        {
            if (breaks) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
            }
            if (!WRITE(emitter, string)) return 0;
            emitter->indention = 0;
            breaks = 0;
        }
    }

    return 1;
}

static int
yaml_emitter_write_folded_scalar(yaml_emitter_t *emitter,
        yaml_char_t *value, size_t length)
{
    yaml_string_t string;
    int breaks = 1;
    int leading_spaces = 1;

    STRING_ASSIGN(string, value, length);

    if (!yaml_emitter_write_indicator(emitter, ">", 1, 0, 0))
        return 0;
    if (!yaml_emitter_write_block_scalar_hints(emitter, string))
        return 0;
    if (!PUT_BREAK(emitter)) return 0;
    emitter->indention = 1;
    emitter->whitespace = 1;

    while (string.pointer != string.end)
    {
        if (IS_BREAK(string))
        {
            if (!breaks && !leading_spaces && CHECK(string, '\n')) {
                int k = 0;
                while (IS_BREAK_AT(string, k)) {
                    k += WIDTH_AT(string, k);
                }
                if (!IS_BLANKZ_AT(string, k)) {
                    if (!PUT_BREAK(emitter)) return 0;
                }
            }
            if (!WRITE_BREAK(emitter, string)) return 0;
            emitter->indention = 1;
            breaks = 1;
        }
        else
        {
            if (breaks) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
                leading_spaces = IS_BLANK(string);
            }
            if (!breaks && IS_SPACE(string) && !IS_SPACE_AT(string, 1)
                    && emitter->column > emitter->best_width) {
                if (!yaml_emitter_write_indent(emitter)) return 0;
                MOVE(string);
            }
            else {
                if (!WRITE(emitter, string)) return 0;
            }
            emitter->indention = 0;
            breaks = 0;
        }
    }

    return 1;
}
