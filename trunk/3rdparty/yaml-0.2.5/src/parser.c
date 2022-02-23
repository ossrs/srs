
/*
 * The parser implements the following grammar:
 *
 * stream               ::= STREAM-START implicit_document? explicit_document* STREAM-END
 * implicit_document    ::= block_node DOCUMENT-END*
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 * block_node_or_indentless_sequence    ::=
 *                          ALIAS
 *                          | properties (block_content | indentless_block_sequence)?
 *                          | block_content
 *                          | indentless_block_sequence
 * block_node           ::= ALIAS
 *                          | properties block_content?
 *                          | block_content
 * flow_node            ::= ALIAS
 *                          | properties flow_content?
 *                          | flow_content
 * properties           ::= TAG ANCHOR? | ANCHOR TAG?
 * block_content        ::= block_collection | flow_collection | SCALAR
 * flow_content         ::= flow_collection | SCALAR
 * block_collection     ::= block_sequence | block_mapping
 * flow_collection      ::= flow_sequence | flow_mapping
 * block_sequence       ::= BLOCK-SEQUENCE-START (BLOCK-ENTRY block_node?)* BLOCK-END
 * indentless_sequence  ::= (BLOCK-ENTRY block_node?)+
 * block_mapping        ::= BLOCK-MAPPING_START
 *                          ((KEY block_node_or_indentless_sequence?)?
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *                          BLOCK-END
 * flow_sequence        ::= FLOW-SEQUENCE-START
 *                          (flow_sequence_entry FLOW-ENTRY)*
 *                          flow_sequence_entry?
 *                          FLOW-SEQUENCE-END
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 * flow_mapping         ::= FLOW-MAPPING-START
 *                          (flow_mapping_entry FLOW-ENTRY)*
 *                          flow_mapping_entry?
 *                          FLOW-MAPPING-END
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 */

#include "yaml_private.h"

/*
 * Peek the next token in the token queue.
 */

#define PEEK_TOKEN(parser)                                                      \
    ((parser->token_available || yaml_parser_fetch_more_tokens(parser)) ?       \
        parser->tokens.head : NULL)

/*
 * Remove the next token from the queue (must be called after PEEK_TOKEN).
 */

#define SKIP_TOKEN(parser)                                                      \
    (parser->token_available = 0,                                               \
     parser->tokens_parsed ++,                                                  \
     parser->stream_end_produced =                                              \
        (parser->tokens.head->type == YAML_STREAM_END_TOKEN),                   \
     parser->tokens.head ++)

/*
 * Public API declarations.
 */

YAML_DECLARE(int)
yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event);

/*
 * Error handling.
 */

static int
yaml_parser_set_parser_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark);

static int
yaml_parser_set_parser_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark);

/*
 * State functions.
 */

static int
yaml_parser_state_machine(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_stream_start(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_document_start(yaml_parser_t *parser, yaml_event_t *event,
        int implicit);

static int
yaml_parser_parse_document_content(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_document_end(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_node(yaml_parser_t *parser, yaml_event_t *event,
        int block, int indentless_sequence);

static int
yaml_parser_parse_block_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_indentless_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_block_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_block_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_flow_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event, int empty);

/*
 * Utility functions.
 */

static int
yaml_parser_process_empty_scalar(yaml_parser_t *parser,
        yaml_event_t *event, yaml_mark_t mark);

static int
yaml_parser_process_directives(yaml_parser_t *parser,
        yaml_version_directive_t **version_directive_ref,
        yaml_tag_directive_t **tag_directives_start_ref,
        yaml_tag_directive_t **tag_directives_end_ref);

static int
yaml_parser_append_tag_directive(yaml_parser_t *parser,
        yaml_tag_directive_t value, int allow_duplicates, yaml_mark_t mark);

/*
 * Get the next event.
 */

YAML_DECLARE(int)
yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event)
{
    assert(parser);     /* Non-NULL parser object is expected. */
    assert(event);      /* Non-NULL event object is expected. */

    /* Erase the event object. */

    memset(event, 0, sizeof(yaml_event_t));

    /* No events after the end of the stream or error. */

    if (parser->stream_end_produced || parser->error ||
            parser->state == YAML_PARSE_END_STATE) {
        return 1;
    }

    /* Generate the next event. */

    return yaml_parser_state_machine(parser, event);
}

/*
 * Set parser error.
 */

static int
yaml_parser_set_parser_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_PARSER_ERROR;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}

static int
yaml_parser_set_parser_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_PARSER_ERROR;
    parser->context = context;
    parser->context_mark = context_mark;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}


/*
 * State dispatcher.
 */

static int
yaml_parser_state_machine(yaml_parser_t *parser, yaml_event_t *event)
{
    switch (parser->state)
    {
        case YAML_PARSE_STREAM_START_STATE:
            return yaml_parser_parse_stream_start(parser, event);

        case YAML_PARSE_IMPLICIT_DOCUMENT_START_STATE:
            return yaml_parser_parse_document_start(parser, event, 1);

        case YAML_PARSE_DOCUMENT_START_STATE:
            return yaml_parser_parse_document_start(parser, event, 0);

        case YAML_PARSE_DOCUMENT_CONTENT_STATE:
            return yaml_parser_parse_document_content(parser, event);

        case YAML_PARSE_DOCUMENT_END_STATE:
            return yaml_parser_parse_document_end(parser, event);

        case YAML_PARSE_BLOCK_NODE_STATE:
            return yaml_parser_parse_node(parser, event, 1, 0);

        case YAML_PARSE_BLOCK_NODE_OR_INDENTLESS_SEQUENCE_STATE:
            return yaml_parser_parse_node(parser, event, 1, 1);

        case YAML_PARSE_FLOW_NODE_STATE:
            return yaml_parser_parse_node(parser, event, 0, 0);

        case YAML_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE:
            return yaml_parser_parse_block_sequence_entry(parser, event, 1);

        case YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_block_sequence_entry(parser, event, 0);

        case YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_indentless_sequence_entry(parser, event);

        case YAML_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE:
            return yaml_parser_parse_block_mapping_key(parser, event, 1);

        case YAML_PARSE_BLOCK_MAPPING_KEY_STATE:
            return yaml_parser_parse_block_mapping_key(parser, event, 0);

        case YAML_PARSE_BLOCK_MAPPING_VALUE_STATE:
            return yaml_parser_parse_block_mapping_value(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE:
            return yaml_parser_parse_flow_sequence_entry(parser, event, 1);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_flow_sequence_entry(parser, event, 0);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_key(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_value(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_end(parser, event);

        case YAML_PARSE_FLOW_MAPPING_FIRST_KEY_STATE:
            return yaml_parser_parse_flow_mapping_key(parser, event, 1);

        case YAML_PARSE_FLOW_MAPPING_KEY_STATE:
            return yaml_parser_parse_flow_mapping_key(parser, event, 0);

        case YAML_PARSE_FLOW_MAPPING_VALUE_STATE:
            return yaml_parser_parse_flow_mapping_value(parser, event, 0);

        case YAML_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE:
            return yaml_parser_parse_flow_mapping_value(parser, event, 1);

        default:
            assert(1);      /* Invalid state. */
    }

    return 0;
}

/*
 * Parse the production:
 * stream   ::= STREAM-START implicit_document? explicit_document* STREAM-END
 *              ************
 */

static int
yaml_parser_parse_stream_start(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_STREAM_START_TOKEN) {
        return yaml_parser_set_parser_error(parser,
                "did not find expected <stream-start>", token->start_mark);
    }

    parser->state = YAML_PARSE_IMPLICIT_DOCUMENT_START_STATE;
    STREAM_START_EVENT_INIT(*event, token->data.stream_start.encoding,
            token->start_mark, token->start_mark);
    SKIP_TOKEN(parser);

    return 1;
}

/*
 * Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                          *
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                          *************************
 */

static int
yaml_parser_parse_document_start(yaml_parser_t *parser, yaml_event_t *event,
        int implicit)
{
    yaml_token_t *token;
    yaml_version_directive_t *version_directive = NULL;
    struct {
        yaml_tag_directive_t *start;
        yaml_tag_directive_t *end;
    } tag_directives = { NULL, NULL };

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    /* Parse extra document end indicators. */

    if (!implicit)
    {
        while (token->type == YAML_DOCUMENT_END_TOKEN) {
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) return 0;
        }
    }

    /* Parse an implicit document. */

    if (implicit && token->type != YAML_VERSION_DIRECTIVE_TOKEN &&
            token->type != YAML_TAG_DIRECTIVE_TOKEN &&
            token->type != YAML_DOCUMENT_START_TOKEN &&
            token->type != YAML_STREAM_END_TOKEN)
    {
        if (!yaml_parser_process_directives(parser, NULL, NULL, NULL))
            return 0;
        if (!PUSH(parser, parser->states, YAML_PARSE_DOCUMENT_END_STATE))
            return 0;
        parser->state = YAML_PARSE_BLOCK_NODE_STATE;
        DOCUMENT_START_EVENT_INIT(*event, NULL, NULL, NULL, 1,
                token->start_mark, token->start_mark);
        return 1;
    }

    /* Parse an explicit document. */

    else if (token->type != YAML_STREAM_END_TOKEN)
    {
        yaml_mark_t start_mark, end_mark;
        start_mark = token->start_mark;
        if (!yaml_parser_process_directives(parser, &version_directive,
                    &tag_directives.start, &tag_directives.end))
            return 0;
        token = PEEK_TOKEN(parser);
        if (!token) goto error;
        if (token->type != YAML_DOCUMENT_START_TOKEN) {
            yaml_parser_set_parser_error(parser,
                    "did not find expected <document start>", token->start_mark);
            goto error;
        }
        if (!PUSH(parser, parser->states, YAML_PARSE_DOCUMENT_END_STATE))
            goto error;
        parser->state = YAML_PARSE_DOCUMENT_CONTENT_STATE;
        end_mark = token->end_mark;
        DOCUMENT_START_EVENT_INIT(*event, version_directive,
                tag_directives.start, tag_directives.end, 0,
                start_mark, end_mark);
        SKIP_TOKEN(parser);
        version_directive = NULL;
        tag_directives.start = tag_directives.end = NULL;
        return 1;
    }

    /* Parse the stream end. */

    else
    {
        parser->state = YAML_PARSE_END_STATE;
        STREAM_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

error:
    yaml_free(version_directive);
    while (tag_directives.start != tag_directives.end) {
        yaml_free(tag_directives.end[-1].handle);
        yaml_free(tag_directives.end[-1].prefix);
        tag_directives.end --;
    }
    yaml_free(tag_directives.start);
    return 0;
}

/*
 * Parse the productions:
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                    ***********
 */

static int
yaml_parser_parse_document_content(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VERSION_DIRECTIVE_TOKEN ||
            token->type == YAML_TAG_DIRECTIVE_TOKEN ||
            token->type == YAML_DOCUMENT_START_TOKEN ||
            token->type == YAML_DOCUMENT_END_TOKEN ||
            token->type == YAML_STREAM_END_TOKEN) {
        parser->state = POP(parser, parser->states);
        return yaml_parser_process_empty_scalar(parser, event,
                token->start_mark);
    }
    else {
        return yaml_parser_parse_node(parser, event, 1, 0);
    }
}

/*
 * Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                                     *************
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                                *************
 */

static int
yaml_parser_parse_document_end(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;
    yaml_mark_t start_mark, end_mark;
    int implicit = 1;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    start_mark = end_mark = token->start_mark;

    if (token->type == YAML_DOCUMENT_END_TOKEN) {
        end_mark = token->end_mark;
        SKIP_TOKEN(parser);
        implicit = 0;
    }

    while (!STACK_EMPTY(parser, parser->tag_directives)) {
        yaml_tag_directive_t tag_directive = POP(parser, parser->tag_directives);
        yaml_free(tag_directive.handle);
        yaml_free(tag_directive.prefix);
    }

    parser->state = YAML_PARSE_DOCUMENT_START_STATE;
    DOCUMENT_END_EVENT_INIT(*event, implicit, start_mark, end_mark);

    return 1;
}

/*
 * Parse the productions:
 * block_node_or_indentless_sequence    ::=
 *                          ALIAS
 *                          *****
 *                          | properties (block_content | indentless_block_sequence)?
 *                            **********  *
 *                          | block_content | indentless_block_sequence
 *                            *
 * block_node           ::= ALIAS
 *                          *****
 *                          | properties block_content?
 *                            ********** *
 *                          | block_content
 *                            *
 * flow_node            ::= ALIAS
 *                          *****
 *                          | properties flow_content?
 *                            ********** *
 *                          | flow_content
 *                            *
 * properties           ::= TAG ANCHOR? | ANCHOR TAG?
 *                          *************************
 * block_content        ::= block_collection | flow_collection | SCALAR
 *                                                               ******
 * flow_content         ::= flow_collection | SCALAR
 *                                            ******
 */

static int
yaml_parser_parse_node(yaml_parser_t *parser, yaml_event_t *event,
        int block, int indentless_sequence)
{
    yaml_token_t *token;
    yaml_char_t *anchor = NULL;
    yaml_char_t *tag_handle = NULL;
    yaml_char_t *tag_suffix = NULL;
    yaml_char_t *tag = NULL;
    yaml_mark_t start_mark, end_mark, tag_mark;
    int implicit;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_ALIAS_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        ALIAS_EVENT_INIT(*event, token->data.alias.value,
                token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        start_mark = end_mark = token->start_mark;

        if (token->type == YAML_ANCHOR_TOKEN)
        {
            anchor = token->data.anchor.value;
            start_mark = token->start_mark;
            end_mark = token->end_mark;
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) goto error;
            if (token->type == YAML_TAG_TOKEN)
            {
                tag_handle = token->data.tag.handle;
                tag_suffix = token->data.tag.suffix;
                tag_mark = token->start_mark;
                end_mark = token->end_mark;
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) goto error;
            }
        }
        else if (token->type == YAML_TAG_TOKEN)
        {
            tag_handle = token->data.tag.handle;
            tag_suffix = token->data.tag.suffix;
            start_mark = tag_mark = token->start_mark;
            end_mark = token->end_mark;
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) goto error;
            if (token->type == YAML_ANCHOR_TOKEN)
            {
                anchor = token->data.anchor.value;
                end_mark = token->end_mark;
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) goto error;
            }
        }

        if (tag_handle) {
            if (!*tag_handle) {
                tag = tag_suffix;
                yaml_free(tag_handle);
                tag_handle = tag_suffix = NULL;
            }
            else {
                yaml_tag_directive_t *tag_directive;
                for (tag_directive = parser->tag_directives.start;
                        tag_directive != parser->tag_directives.top;
                        tag_directive ++) {
                    if (strcmp((char *)tag_directive->handle, (char *)tag_handle) == 0) {
                        size_t prefix_len = strlen((char *)tag_directive->prefix);
                        size_t suffix_len = strlen((char *)tag_suffix);
                        tag = YAML_MALLOC(prefix_len+suffix_len+1);
                        if (!tag) {
                            parser->error = YAML_MEMORY_ERROR;
                            goto error;
                        }
                        memcpy(tag, tag_directive->prefix, prefix_len);
                        memcpy(tag+prefix_len, tag_suffix, suffix_len);
                        tag[prefix_len+suffix_len] = '\0';
                        yaml_free(tag_handle);
                        yaml_free(tag_suffix);
                        tag_handle = tag_suffix = NULL;
                        break;
                    }
                }
                if (!tag) {
                    yaml_parser_set_parser_error_context(parser,
                            "while parsing a node", start_mark,
                            "found undefined tag handle", tag_mark);
                    goto error;
                }
            }
        }

        implicit = (!tag || !*tag);
        if (indentless_sequence && token->type == YAML_BLOCK_ENTRY_TOKEN) {
            end_mark = token->end_mark;
            parser->state = YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
            SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                    YAML_BLOCK_SEQUENCE_STYLE, start_mark, end_mark);
            return 1;
        }
        else {
            if (token->type == YAML_SCALAR_TOKEN) {
                int plain_implicit = 0;
                int quoted_implicit = 0;
                end_mark = token->end_mark;
                if ((token->data.scalar.style == YAML_PLAIN_SCALAR_STYLE && !tag)
                        || (tag && strcmp((char *)tag, "!") == 0)) {
                    plain_implicit = 1;
                }
                else if (!tag) {
                    quoted_implicit = 1;
                }
                parser->state = POP(parser, parser->states);
                SCALAR_EVENT_INIT(*event, anchor, tag,
                        token->data.scalar.value, token->data.scalar.length,
                        plain_implicit, quoted_implicit,
                        token->data.scalar.style, start_mark, end_mark);
                SKIP_TOKEN(parser);
                return 1;
            }
            else if (token->type == YAML_FLOW_SEQUENCE_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE;
                SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_FLOW_SEQUENCE_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (token->type == YAML_FLOW_MAPPING_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_FLOW_MAPPING_FIRST_KEY_STATE;
                MAPPING_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_FLOW_MAPPING_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (block && token->type == YAML_BLOCK_SEQUENCE_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE;
                SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_BLOCK_SEQUENCE_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (block && token->type == YAML_BLOCK_MAPPING_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE;
                MAPPING_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_BLOCK_MAPPING_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (anchor || tag) {
                yaml_char_t *value = YAML_MALLOC(1);
                if (!value) {
                    parser->error = YAML_MEMORY_ERROR;
                    goto error;
                }
                value[0] = '\0';
                parser->state = POP(parser, parser->states);
                SCALAR_EVENT_INIT(*event, anchor, tag, value, 0,
                        implicit, 0, YAML_PLAIN_SCALAR_STYLE,
                        start_mark, end_mark);
                return 1;
            }
            else {
                yaml_parser_set_parser_error_context(parser,
                        (block ? "while parsing a block node"
                         : "while parsing a flow node"), start_mark,
                        "did not find expected node content", token->start_mark);
                goto error;
            }
        }
    }

error:
    yaml_free(anchor);
    yaml_free(tag_handle);
    yaml_free(tag_suffix);
    yaml_free(tag);

    return 0;
}

/*
 * Parse the productions:
 * block_sequence ::= BLOCK-SEQUENCE-START (BLOCK-ENTRY block_node?)* BLOCK-END
 *                    ********************  *********** *             *********
 */

static int
yaml_parser_parse_block_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_BLOCK_ENTRY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_BLOCK_ENTRY_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 0);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else if (token->type == YAML_BLOCK_END_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        (void)POP(parser, parser->marks);
        SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        return yaml_parser_set_parser_error_context(parser,
                "while parsing a block collection", POP(parser, parser->marks),
                "did not find expected '-' indicator", token->start_mark);
    }
}

/*
 * Parse the productions:
 * indentless_sequence  ::= (BLOCK-ENTRY block_node?)+
 *                           *********** *
 */

static int
yaml_parser_parse_indentless_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_BLOCK_ENTRY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_BLOCK_ENTRY_TOKEN &&
                token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 0);
        }
        else {
            parser->state = YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else
    {
        parser->state = POP(parser, parser->states);
        SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->start_mark);
        return 1;
    }
}

/*
 * Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *                          *******************
 *                          ((KEY block_node_or_indentless_sequence?)?
 *                            *** *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *
 *                          BLOCK-END
 *                          *********
 */

static int
yaml_parser_parse_block_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_KEY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_MAPPING_VALUE_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 1);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_MAPPING_VALUE_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else if (token->type == YAML_BLOCK_END_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        (void)POP(parser, parser->marks);
        MAPPING_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        return yaml_parser_set_parser_error_context(parser,
                "while parsing a block mapping", POP(parser, parser->marks),
                "did not find expected key", token->start_mark);
    }
}

/*
 * Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *
 *                          ((KEY block_node_or_indentless_sequence?)?
 *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *                           ***** *
 *                          BLOCK-END
 *
 */

static int
yaml_parser_parse_block_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VALUE_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_MAPPING_KEY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 1);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_MAPPING_KEY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else
    {
        parser->state = YAML_PARSE_BLOCK_MAPPING_KEY_STATE;
        return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
    }
}

/*
 * Parse the productions:
 * flow_sequence        ::= FLOW-SEQUENCE-START
 *                          *******************
 *                          (flow_sequence_entry FLOW-ENTRY)*
 *                           *                   **********
 *                          flow_sequence_entry?
 *                          *
 *                          FLOW-SEQUENCE-END
 *                          *****************
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          *
 */

static int
yaml_parser_parse_flow_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_FLOW_SEQUENCE_END_TOKEN)
    {
        if (!first) {
            if (token->type == YAML_FLOW_ENTRY_TOKEN) {
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) return 0;
            }
            else {
                return yaml_parser_set_parser_error_context(parser,
                        "while parsing a flow sequence", POP(parser, parser->marks),
                        "did not find expected ',' or ']'", token->start_mark);
            }
        }

        if (token->type == YAML_KEY_TOKEN) {
            parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE;
            MAPPING_START_EVENT_INIT(*event, NULL, NULL,
                    1, YAML_FLOW_MAPPING_STYLE,
                    token->start_mark, token->end_mark);
            SKIP_TOKEN(parser);
            return 1;
        }

        else if (token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = POP(parser, parser->states);
    (void)POP(parser, parser->marks);
    SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
    SKIP_TOKEN(parser);
    return 1;
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                      *** *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_VALUE_TOKEN && token->type != YAML_FLOW_ENTRY_TOKEN
            && token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
        if (!PUSH(parser, parser->states,
                    YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE))
            return 0;
        return yaml_parser_parse_node(parser, event, 0, 0);
    }
    else {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE;
        return yaml_parser_process_empty_scalar(parser, event, mark);
    }
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                      ***** *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VALUE_TOKEN) {
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_FLOW_ENTRY_TOKEN
                && token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }
    parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE;
    return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                                      *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE;

    MAPPING_END_EVENT_INIT(*event, token->start_mark, token->start_mark);
    return 1;
}

/*
 * Parse the productions:
 * flow_mapping         ::= FLOW-MAPPING-START
 *                          ******************
 *                          (flow_mapping_entry FLOW-ENTRY)*
 *                           *                  **********
 *                          flow_mapping_entry?
 *                          ******************
 *                          FLOW-MAPPING-END
 *                          ****************
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          *           *** *
 */

static int
yaml_parser_parse_flow_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_FLOW_MAPPING_END_TOKEN)
    {
        if (!first) {
            if (token->type == YAML_FLOW_ENTRY_TOKEN) {
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) return 0;
            }
            else {
                return yaml_parser_set_parser_error_context(parser,
                        "while parsing a flow mapping", POP(parser, parser->marks),
                        "did not find expected ',' or '}'", token->start_mark);
            }
        }

        if (token->type == YAML_KEY_TOKEN) {
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) return 0;
            if (token->type != YAML_VALUE_TOKEN
                    && token->type != YAML_FLOW_ENTRY_TOKEN
                    && token->type != YAML_FLOW_MAPPING_END_TOKEN) {
                if (!PUSH(parser, parser->states,
                            YAML_PARSE_FLOW_MAPPING_VALUE_STATE))
                    return 0;
                return yaml_parser_parse_node(parser, event, 0, 0);
            }
            else {
                parser->state = YAML_PARSE_FLOW_MAPPING_VALUE_STATE;
                return yaml_parser_process_empty_scalar(parser, event,
                        token->start_mark);
            }
        }
        else if (token->type != YAML_FLOW_MAPPING_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = POP(parser, parser->states);
    (void)POP(parser, parser->marks);
    MAPPING_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
    SKIP_TOKEN(parser);
    return 1;
}

/*
 * Parse the productions:
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                   *                  ***** *
 */

static int
yaml_parser_parse_flow_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event, int empty)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (empty) {
        parser->state = YAML_PARSE_FLOW_MAPPING_KEY_STATE;
        return yaml_parser_process_empty_scalar(parser, event,
                token->start_mark);
    }

    if (token->type == YAML_VALUE_TOKEN) {
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_FLOW_ENTRY_TOKEN
                && token->type != YAML_FLOW_MAPPING_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_MAPPING_KEY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = YAML_PARSE_FLOW_MAPPING_KEY_STATE;
    return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
}

/*
 * Generate an empty scalar event.
 */

static int
yaml_parser_process_empty_scalar(yaml_parser_t *parser, yaml_event_t *event,
        yaml_mark_t mark)
{
    yaml_char_t *value;

    value = YAML_MALLOC(1);
    if (!value) {
        parser->error = YAML_MEMORY_ERROR;
        return 0;
    }
    value[0] = '\0';

    SCALAR_EVENT_INIT(*event, NULL, NULL, value, 0,
            1, 0, YAML_PLAIN_SCALAR_STYLE, mark, mark);

    return 1;
}

/*
 * Parse directives.
 */

static int
yaml_parser_process_directives(yaml_parser_t *parser,
        yaml_version_directive_t **version_directive_ref,
        yaml_tag_directive_t **tag_directives_start_ref,
        yaml_tag_directive_t **tag_directives_end_ref)
{
    yaml_tag_directive_t default_tag_directives[] = {
        {(yaml_char_t *)"!", (yaml_char_t *)"!"},
        {(yaml_char_t *)"!!", (yaml_char_t *)"tag:yaml.org,2002:"},
        {NULL, NULL}
    };
    yaml_tag_directive_t *default_tag_directive;
    yaml_version_directive_t *version_directive = NULL;
    struct {
        yaml_tag_directive_t *start;
        yaml_tag_directive_t *end;
        yaml_tag_directive_t *top;
    } tag_directives = { NULL, NULL, NULL };
    yaml_token_t *token;

    if (!STACK_INIT(parser, tag_directives, yaml_tag_directive_t*))
        goto error;

    token = PEEK_TOKEN(parser);
    if (!token) goto error;

    while (token->type == YAML_VERSION_DIRECTIVE_TOKEN ||
            token->type == YAML_TAG_DIRECTIVE_TOKEN)
    {
        if (token->type == YAML_VERSION_DIRECTIVE_TOKEN) {
            if (version_directive) {
                yaml_parser_set_parser_error(parser,
                        "found duplicate %YAML directive", token->start_mark);
                goto error;
            }
            if (token->data.version_directive.major != 1
                    || (
                        token->data.version_directive.minor != 1
                        && token->data.version_directive.minor != 2
                    )) {
                yaml_parser_set_parser_error(parser,
                        "found incompatible YAML document", token->start_mark);
                goto error;
            }
            version_directive = YAML_MALLOC_STATIC(yaml_version_directive_t);
            if (!version_directive) {
                parser->error = YAML_MEMORY_ERROR;
                goto error;
            }
            version_directive->major = token->data.version_directive.major;
            version_directive->minor = token->data.version_directive.minor;
        }

        else if (token->type == YAML_TAG_DIRECTIVE_TOKEN) {
            yaml_tag_directive_t value;
            value.handle = token->data.tag_directive.handle;
            value.prefix = token->data.tag_directive.prefix;

            if (!yaml_parser_append_tag_directive(parser, value, 0,
                        token->start_mark))
                goto error;
            if (!PUSH(parser, tag_directives, value))
                goto error;
        }

        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) goto error;
    }

    for (default_tag_directive = default_tag_directives;
            default_tag_directive->handle; default_tag_directive++) {
        if (!yaml_parser_append_tag_directive(parser, *default_tag_directive, 1,
                    token->start_mark))
            goto error;
    }

    if (version_directive_ref) {
        *version_directive_ref = version_directive;
    }
    if (tag_directives_start_ref) {
        if (STACK_EMPTY(parser, tag_directives)) {
            *tag_directives_start_ref = *tag_directives_end_ref = NULL;
            STACK_DEL(parser, tag_directives);
        }
        else {
            *tag_directives_start_ref = tag_directives.start;
            *tag_directives_end_ref = tag_directives.top;
        }
    }
    else {
        STACK_DEL(parser, tag_directives);
    }

    if (!version_directive_ref)
        yaml_free(version_directive);
    return 1;

error:
    yaml_free(version_directive);
    while (!STACK_EMPTY(parser, tag_directives)) {
        yaml_tag_directive_t tag_directive = POP(parser, tag_directives);
        yaml_free(tag_directive.handle);
        yaml_free(tag_directive.prefix);
    }
    STACK_DEL(parser, tag_directives);
    return 0;
}

/*
 * Append a tag directive to the directives stack.
 */

static int
yaml_parser_append_tag_directive(yaml_parser_t *parser,
        yaml_tag_directive_t value, int allow_duplicates, yaml_mark_t mark)
{
    yaml_tag_directive_t *tag_directive;
    yaml_tag_directive_t copy = { NULL, NULL };

    for (tag_directive = parser->tag_directives.start;
            tag_directive != parser->tag_directives.top; tag_directive ++) {
        if (strcmp((char *)value.handle, (char *)tag_directive->handle) == 0) {
            if (allow_duplicates)
                return 1;
            return yaml_parser_set_parser_error(parser,
                    "found duplicate %TAG directive", mark);
        }
    }

    copy.handle = yaml_strdup(value.handle);
    copy.prefix = yaml_strdup(value.prefix);
    if (!copy.handle || !copy.prefix) {
        parser->error = YAML_MEMORY_ERROR;
        goto error;
    }

    if (!PUSH(parser, parser->tag_directives, copy))
        goto error;

    return 1;

error:
    yaml_free(copy.handle);
    yaml_free(copy.prefix);
    return 0;
}

