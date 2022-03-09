
#include "yaml_private.h"

/*
 * API functions.
 */

YAML_DECLARE(int)
yaml_parser_load(yaml_parser_t *parser, yaml_document_t *document);

/*
 * Error handling.
 */

static int
yaml_parser_set_composer_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark);

static int
yaml_parser_set_composer_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark);


/*
 * Alias handling.
 */

static int
yaml_parser_register_anchor(yaml_parser_t *parser,
        int index, yaml_char_t *anchor);

/*
 * Clean up functions.
 */

static void
yaml_parser_delete_aliases(yaml_parser_t *parser);

/*
 * Document loading context.
 */
struct loader_ctx {
    int *start;
    int *end;
    int *top;
};

/*
 * Composer functions.
 */
static int
yaml_parser_load_nodes(yaml_parser_t *parser, struct loader_ctx *ctx);

static int
yaml_parser_load_document(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_load_alias(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

static int
yaml_parser_load_scalar(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

static int
yaml_parser_load_sequence(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

static int
yaml_parser_load_mapping(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

static int
yaml_parser_load_sequence_end(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

static int
yaml_parser_load_mapping_end(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx);

/*
 * Load the next document of the stream.
 */

YAML_DECLARE(int)
yaml_parser_load(yaml_parser_t *parser, yaml_document_t *document)
{
    yaml_event_t event;

    assert(parser);     /* Non-NULL parser object is expected. */
    assert(document);   /* Non-NULL document object is expected. */

    memset(document, 0, sizeof(yaml_document_t));
    if (!STACK_INIT(parser, document->nodes, yaml_node_t*))
        goto error;

    if (!parser->stream_start_produced) {
        if (!yaml_parser_parse(parser, &event)) goto error;
        assert(event.type == YAML_STREAM_START_EVENT);
                        /* STREAM-START is expected. */
    }

    if (parser->stream_end_produced) {
        return 1;
    }

    if (!yaml_parser_parse(parser, &event)) goto error;
    if (event.type == YAML_STREAM_END_EVENT) {
        return 1;
    }

    if (!STACK_INIT(parser, parser->aliases, yaml_alias_data_t*))
        goto error;

    parser->document = document;

    if (!yaml_parser_load_document(parser, &event)) goto error;

    yaml_parser_delete_aliases(parser);
    parser->document = NULL;

    return 1;

error:

    yaml_parser_delete_aliases(parser);
    yaml_document_delete(document);
    parser->document = NULL;

    return 0;
}

/*
 * Set composer error.
 */

static int
yaml_parser_set_composer_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_COMPOSER_ERROR;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}

/*
 * Set composer error with context.
 */

static int
yaml_parser_set_composer_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_COMPOSER_ERROR;
    parser->context = context;
    parser->context_mark = context_mark;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}

/*
 * Delete the stack of aliases.
 */

static void
yaml_parser_delete_aliases(yaml_parser_t *parser)
{
    while (!STACK_EMPTY(parser, parser->aliases)) {
        yaml_free(POP(parser, parser->aliases).anchor);
    }
    STACK_DEL(parser, parser->aliases);
}

/*
 * Compose a document object.
 */

static int
yaml_parser_load_document(yaml_parser_t *parser, yaml_event_t *event)
{
    struct loader_ctx ctx = { NULL, NULL, NULL };

    assert(event->type == YAML_DOCUMENT_START_EVENT);
                        /* DOCUMENT-START is expected. */

    parser->document->version_directive
        = event->data.document_start.version_directive;
    parser->document->tag_directives.start
        = event->data.document_start.tag_directives.start;
    parser->document->tag_directives.end
        = event->data.document_start.tag_directives.end;
    parser->document->start_implicit
        = event->data.document_start.implicit;
    parser->document->start_mark = event->start_mark;

    if (!STACK_INIT(parser, ctx, int*)) return 0;
    if (!yaml_parser_load_nodes(parser, &ctx)) {
        STACK_DEL(parser, ctx);
        return 0;
    }
    STACK_DEL(parser, ctx);

    return 1;
}

/*
 * Compose a node tree.
 */

static int
yaml_parser_load_nodes(yaml_parser_t *parser, struct loader_ctx *ctx)
{
    yaml_event_t event;

    do {
        if (!yaml_parser_parse(parser, &event)) return 0;

        switch (event.type) {
            case YAML_ALIAS_EVENT:
                if (!yaml_parser_load_alias(parser, &event, ctx)) return 0;
                break;
            case YAML_SCALAR_EVENT:
                if (!yaml_parser_load_scalar(parser, &event, ctx)) return 0;
                break;
            case YAML_SEQUENCE_START_EVENT:
                if (!yaml_parser_load_sequence(parser, &event, ctx)) return 0;
                break;
            case YAML_SEQUENCE_END_EVENT:
                if (!yaml_parser_load_sequence_end(parser, &event, ctx))
                    return 0;
                break;
            case YAML_MAPPING_START_EVENT:
                if (!yaml_parser_load_mapping(parser, &event, ctx)) return 0;
                break;
            case YAML_MAPPING_END_EVENT:
                if (!yaml_parser_load_mapping_end(parser, &event, ctx))
                    return 0;
                break;
            default:
                assert(0);  /* Could not happen. */
                return 0;
            case YAML_DOCUMENT_END_EVENT:
                break;
        }
    } while (event.type != YAML_DOCUMENT_END_EVENT);

    parser->document->end_implicit = event.data.document_end.implicit;
    parser->document->end_mark = event.end_mark;

    return 1;
}

/*
 * Add an anchor.
 */

static int
yaml_parser_register_anchor(yaml_parser_t *parser,
        int index, yaml_char_t *anchor)
{
    yaml_alias_data_t data;
    yaml_alias_data_t *alias_data;

    if (!anchor) return 1;

    data.anchor = anchor;
    data.index = index;
    data.mark = parser->document->nodes.start[index-1].start_mark;

    for (alias_data = parser->aliases.start;
            alias_data != parser->aliases.top; alias_data ++) {
        if (strcmp((char *)alias_data->anchor, (char *)anchor) == 0) {
            yaml_free(anchor);
            return yaml_parser_set_composer_error_context(parser,
                    "found duplicate anchor; first occurrence",
                    alias_data->mark, "second occurrence", data.mark);
        }
    }

    if (!PUSH(parser, parser->aliases, data)) {
        yaml_free(anchor);
        return 0;
    }

    return 1;
}

/*
 * Compose node into its parent in the stree.
 */

static int
yaml_parser_load_node_add(yaml_parser_t *parser, struct loader_ctx *ctx,
        int index)
{
    struct yaml_node_s *parent;
    int parent_index;

    if (STACK_EMPTY(parser, *ctx)) {
        /* This is the root node, there's no tree to add it to. */
        return 1;
    }

    parent_index = *((*ctx).top - 1);
    parent = &parser->document->nodes.start[parent_index-1];

    switch (parent->type) {
        case YAML_SEQUENCE_NODE:
            if (!STACK_LIMIT(parser, parent->data.sequence.items, INT_MAX-1))
                return 0;
            if (!PUSH(parser, parent->data.sequence.items, index))
                return 0;
            break;
        case YAML_MAPPING_NODE: {
            yaml_node_pair_t pair;
            if (!STACK_EMPTY(parser, parent->data.mapping.pairs)) {
                yaml_node_pair_t *p = parent->data.mapping.pairs.top - 1;
                if (p->key != 0 && p->value == 0) {
                    p->value = index;
                    break;
                }
            }

            pair.key = index;
            pair.value = 0;
            if (!STACK_LIMIT(parser, parent->data.mapping.pairs, INT_MAX-1))
                return 0;
            if (!PUSH(parser, parent->data.mapping.pairs, pair))
                return 0;

            break;
        }
        default:
            assert(0); /* Could not happen. */
            return 0;
    }
    return 1;
}

/*
 * Compose a node corresponding to an alias.
 */

static int
yaml_parser_load_alias(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    yaml_char_t *anchor = event->data.alias.anchor;
    yaml_alias_data_t *alias_data;

    for (alias_data = parser->aliases.start;
            alias_data != parser->aliases.top; alias_data ++) {
        if (strcmp((char *)alias_data->anchor, (char *)anchor) == 0) {
            yaml_free(anchor);
            return yaml_parser_load_node_add(parser, ctx, alias_data->index);
        }
    }

    yaml_free(anchor);
    return yaml_parser_set_composer_error(parser, "found undefined alias",
            event->start_mark);
}

/*
 * Compose a scalar node.
 */

static int
yaml_parser_load_scalar(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    yaml_node_t node;
    int index;
    yaml_char_t *tag = event->data.scalar.tag;

    if (!STACK_LIMIT(parser, parser->document->nodes, INT_MAX-1)) goto error;

    if (!tag || strcmp((char *)tag, "!") == 0) {
        yaml_free(tag);
        tag = yaml_strdup((yaml_char_t *)YAML_DEFAULT_SCALAR_TAG);
        if (!tag) goto error;
    }

    SCALAR_NODE_INIT(node, tag, event->data.scalar.value,
            event->data.scalar.length, event->data.scalar.style,
            event->start_mark, event->end_mark);

    if (!PUSH(parser, parser->document->nodes, node)) goto error;

    index = parser->document->nodes.top - parser->document->nodes.start;

    if (!yaml_parser_register_anchor(parser, index,
                event->data.scalar.anchor)) return 0;

    return yaml_parser_load_node_add(parser, ctx, index);

error:
    yaml_free(tag);
    yaml_free(event->data.scalar.anchor);
    yaml_free(event->data.scalar.value);
    return 0;
}

/*
 * Compose a sequence node.
 */

static int
yaml_parser_load_sequence(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    yaml_node_t node;
    struct {
        yaml_node_item_t *start;
        yaml_node_item_t *end;
        yaml_node_item_t *top;
    } items = { NULL, NULL, NULL };
    int index;
    yaml_char_t *tag = event->data.sequence_start.tag;

    if (!STACK_LIMIT(parser, parser->document->nodes, INT_MAX-1)) goto error;

    if (!tag || strcmp((char *)tag, "!") == 0) {
        yaml_free(tag);
        tag = yaml_strdup((yaml_char_t *)YAML_DEFAULT_SEQUENCE_TAG);
        if (!tag) goto error;
    }

    if (!STACK_INIT(parser, items, yaml_node_item_t*)) goto error;

    SEQUENCE_NODE_INIT(node, tag, items.start, items.end,
            event->data.sequence_start.style,
            event->start_mark, event->end_mark);

    if (!PUSH(parser, parser->document->nodes, node)) goto error;

    index = parser->document->nodes.top - parser->document->nodes.start;

    if (!yaml_parser_register_anchor(parser, index,
                event->data.sequence_start.anchor)) return 0;

    if (!yaml_parser_load_node_add(parser, ctx, index)) return 0;

    if (!STACK_LIMIT(parser, *ctx, INT_MAX-1)) return 0;
    if (!PUSH(parser, *ctx, index)) return 0;

    return 1;

error:
    yaml_free(tag);
    yaml_free(event->data.sequence_start.anchor);
    return 0;
}

static int
yaml_parser_load_sequence_end(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    int index;

    assert(((*ctx).top - (*ctx).start) > 0);

    index = *((*ctx).top - 1);
    assert(parser->document->nodes.start[index-1].type == YAML_SEQUENCE_NODE);
    parser->document->nodes.start[index-1].end_mark = event->end_mark;

    (void)POP(parser, *ctx);

    return 1;
}

/*
 * Compose a mapping node.
 */

static int
yaml_parser_load_mapping(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    yaml_node_t node;
    struct {
        yaml_node_pair_t *start;
        yaml_node_pair_t *end;
        yaml_node_pair_t *top;
    } pairs = { NULL, NULL, NULL };
    int index;
    yaml_char_t *tag = event->data.mapping_start.tag;

    if (!STACK_LIMIT(parser, parser->document->nodes, INT_MAX-1)) goto error;

    if (!tag || strcmp((char *)tag, "!") == 0) {
        yaml_free(tag);
        tag = yaml_strdup((yaml_char_t *)YAML_DEFAULT_MAPPING_TAG);
        if (!tag) goto error;
    }

    if (!STACK_INIT(parser, pairs, yaml_node_pair_t*)) goto error;

    MAPPING_NODE_INIT(node, tag, pairs.start, pairs.end,
            event->data.mapping_start.style,
            event->start_mark, event->end_mark);

    if (!PUSH(parser, parser->document->nodes, node)) goto error;

    index = parser->document->nodes.top - parser->document->nodes.start;

    if (!yaml_parser_register_anchor(parser, index,
                event->data.mapping_start.anchor)) return 0;

    if (!yaml_parser_load_node_add(parser, ctx, index)) return 0;

    if (!STACK_LIMIT(parser, *ctx, INT_MAX-1)) return 0;
    if (!PUSH(parser, *ctx, index)) return 0;

    return 1;

error:
    yaml_free(tag);
    yaml_free(event->data.mapping_start.anchor);
    return 0;
}

static int
yaml_parser_load_mapping_end(yaml_parser_t *parser, yaml_event_t *event,
        struct loader_ctx *ctx)
{
    int index;

    assert(((*ctx).top - (*ctx).start) > 0);

    index = *((*ctx).top - 1);
    assert(parser->document->nodes.start[index-1].type == YAML_MAPPING_NODE);
    parser->document->nodes.start[index-1].end_mark = event->end_mark;

    (void)POP(parser, *ctx);

    return 1;
}