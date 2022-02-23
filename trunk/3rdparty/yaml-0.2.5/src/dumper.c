
#include "yaml_private.h"

/*
 * API functions.
 */

YAML_DECLARE(int)
yaml_emitter_open(yaml_emitter_t *emitter);

YAML_DECLARE(int)
yaml_emitter_close(yaml_emitter_t *emitter);

YAML_DECLARE(int)
yaml_emitter_dump(yaml_emitter_t *emitter, yaml_document_t *document);

/*
 * Clean up functions.
 */

static void
yaml_emitter_delete_document_and_anchors(yaml_emitter_t *emitter);

/*
 * Anchor functions.
 */

static void
yaml_emitter_anchor_node(yaml_emitter_t *emitter, int index);

static yaml_char_t *
yaml_emitter_generate_anchor(yaml_emitter_t *emitter, int anchor_id);


/*
 * Serialize functions.
 */

static int
yaml_emitter_dump_node(yaml_emitter_t *emitter, int index);

static int
yaml_emitter_dump_alias(yaml_emitter_t *emitter, yaml_char_t *anchor);

static int
yaml_emitter_dump_scalar(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor);

static int
yaml_emitter_dump_sequence(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor);

static int
yaml_emitter_dump_mapping(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor);

/*
 * Issue a STREAM-START event.
 */

YAML_DECLARE(int)
yaml_emitter_open(yaml_emitter_t *emitter)
{
    yaml_event_t event;
    yaml_mark_t mark = { 0, 0, 0 };

    assert(emitter);            /* Non-NULL emitter object is required. */
    assert(!emitter->opened);   /* Emitter should not be opened yet. */

    STREAM_START_EVENT_INIT(event, YAML_ANY_ENCODING, mark, mark);

    if (!yaml_emitter_emit(emitter, &event)) {
        return 0;
    }

    emitter->opened = 1;

    return 1;
}

/*
 * Issue a STREAM-END event.
 */

YAML_DECLARE(int)
yaml_emitter_close(yaml_emitter_t *emitter)
{
    yaml_event_t event;
    yaml_mark_t mark = { 0, 0, 0 };

    assert(emitter);            /* Non-NULL emitter object is required. */
    assert(emitter->opened);    /* Emitter should be opened. */

    if (emitter->closed) return 1;

    STREAM_END_EVENT_INIT(event, mark, mark);

    if (!yaml_emitter_emit(emitter, &event)) {
        return 0;
    }

    emitter->closed = 1;

    return 1;
}

/*
 * Dump a YAML document.
 */

YAML_DECLARE(int)
yaml_emitter_dump(yaml_emitter_t *emitter, yaml_document_t *document)
{
    yaml_event_t event;
    yaml_mark_t mark = { 0, 0, 0 };

    assert(emitter);            /* Non-NULL emitter object is required. */
    assert(document);           /* Non-NULL emitter object is expected. */

    emitter->document = document;

    if (!emitter->opened) {
        if (!yaml_emitter_open(emitter)) goto error;
    }

    if (STACK_EMPTY(emitter, document->nodes)) {
        if (!yaml_emitter_close(emitter)) goto error;
        yaml_emitter_delete_document_and_anchors(emitter);
        return 1;
    }

    assert(emitter->opened);    /* Emitter should be opened. */

    emitter->anchors = (yaml_anchors_t*)yaml_malloc(sizeof(*(emitter->anchors))
            * (document->nodes.top - document->nodes.start));
    if (!emitter->anchors) goto error;
    memset(emitter->anchors, 0, sizeof(*(emitter->anchors))
            * (document->nodes.top - document->nodes.start));

    DOCUMENT_START_EVENT_INIT(event, document->version_directive,
            document->tag_directives.start, document->tag_directives.end,
            document->start_implicit, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) goto error;

    yaml_emitter_anchor_node(emitter, 1);
    if (!yaml_emitter_dump_node(emitter, 1)) goto error;

    DOCUMENT_END_EVENT_INIT(event, document->end_implicit, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) goto error;

    yaml_emitter_delete_document_and_anchors(emitter);

    return 1;

error:

    yaml_emitter_delete_document_and_anchors(emitter);

    return 0;
}

/*
 * Clean up the emitter object after a document is dumped.
 */

static void
yaml_emitter_delete_document_and_anchors(yaml_emitter_t *emitter)
{
    int index;

    if (!emitter->anchors) {
        yaml_document_delete(emitter->document);
        emitter->document = NULL;
        return;
    }

    for (index = 0; emitter->document->nodes.start + index
            < emitter->document->nodes.top; index ++) {
        yaml_node_t node = emitter->document->nodes.start[index];
        if (!emitter->anchors[index].serialized) {
            yaml_free(node.tag);
            if (node.type == YAML_SCALAR_NODE) {
                yaml_free(node.data.scalar.value);
            }
        }
        if (node.type == YAML_SEQUENCE_NODE) {
            STACK_DEL(emitter, node.data.sequence.items);
        }
        if (node.type == YAML_MAPPING_NODE) {
            STACK_DEL(emitter, node.data.mapping.pairs);
        }
    }

    STACK_DEL(emitter, emitter->document->nodes);
    yaml_free(emitter->anchors);

    emitter->anchors = NULL;
    emitter->last_anchor_id = 0;
    emitter->document = NULL;
}

/*
 * Check the references of a node and assign the anchor id if needed.
 */

static void
yaml_emitter_anchor_node(yaml_emitter_t *emitter, int index)
{
    yaml_node_t *node = emitter->document->nodes.start + index - 1;
    yaml_node_item_t *item;
    yaml_node_pair_t *pair;

    emitter->anchors[index-1].references ++;

    if (emitter->anchors[index-1].references == 1) {
        switch (node->type) {
            case YAML_SEQUENCE_NODE:
                for (item = node->data.sequence.items.start;
                        item < node->data.sequence.items.top; item ++) {
                    yaml_emitter_anchor_node(emitter, *item);
                }
                break;
            case YAML_MAPPING_NODE:
                for (pair = node->data.mapping.pairs.start;
                        pair < node->data.mapping.pairs.top; pair ++) {
                    yaml_emitter_anchor_node(emitter, pair->key);
                    yaml_emitter_anchor_node(emitter, pair->value);
                }
                break;
            default:
                break;
        }
    }

    else if (emitter->anchors[index-1].references == 2) {
        emitter->anchors[index-1].anchor = (++ emitter->last_anchor_id);
    }
}

/*
 * Generate a textual representation for an anchor.
 */

#define ANCHOR_TEMPLATE         "id%03d"
#define ANCHOR_TEMPLATE_LENGTH  16

static yaml_char_t *
yaml_emitter_generate_anchor(SHIM(yaml_emitter_t *emitter), int anchor_id)
{
    yaml_char_t *anchor = YAML_MALLOC(ANCHOR_TEMPLATE_LENGTH);

    if (!anchor) return NULL;

    sprintf((char *)anchor, ANCHOR_TEMPLATE, anchor_id);

    return anchor;
}

/*
 * Serialize a node.
 */

static int
yaml_emitter_dump_node(yaml_emitter_t *emitter, int index)
{
    yaml_node_t *node = emitter->document->nodes.start + index - 1;
    int anchor_id = emitter->anchors[index-1].anchor;
    yaml_char_t *anchor = NULL;

    if (anchor_id) {
        anchor = yaml_emitter_generate_anchor(emitter, anchor_id);
        if (!anchor) return 0;
    }

    if (emitter->anchors[index-1].serialized) {
        return yaml_emitter_dump_alias(emitter, anchor);
    }

    emitter->anchors[index-1].serialized = 1;

    switch (node->type) {
        case YAML_SCALAR_NODE:
            return yaml_emitter_dump_scalar(emitter, node, anchor);
        case YAML_SEQUENCE_NODE:
            return yaml_emitter_dump_sequence(emitter, node, anchor);
        case YAML_MAPPING_NODE:
            return yaml_emitter_dump_mapping(emitter, node, anchor);
        default:
            assert(0);      /* Could not happen. */
            break;
    }

    return 0;       /* Could not happen. */
}

/*
 * Serialize an alias.
 */

static int
yaml_emitter_dump_alias(yaml_emitter_t *emitter, yaml_char_t *anchor)
{
    yaml_event_t event;
    yaml_mark_t mark  = { 0, 0, 0 };

    ALIAS_EVENT_INIT(event, anchor, mark, mark);

    return yaml_emitter_emit(emitter, &event);
}

/*
 * Serialize a scalar.
 */

static int
yaml_emitter_dump_scalar(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor)
{
    yaml_event_t event;
    yaml_mark_t mark  = { 0, 0, 0 };

    int plain_implicit = (strcmp((char *)node->tag,
                YAML_DEFAULT_SCALAR_TAG) == 0);
    int quoted_implicit = (strcmp((char *)node->tag,
                YAML_DEFAULT_SCALAR_TAG) == 0);

    SCALAR_EVENT_INIT(event, anchor, node->tag, node->data.scalar.value,
            node->data.scalar.length, plain_implicit, quoted_implicit,
            node->data.scalar.style, mark, mark);

    return yaml_emitter_emit(emitter, &event);
}

/*
 * Serialize a sequence.
 */

static int
yaml_emitter_dump_sequence(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor)
{
    yaml_event_t event;
    yaml_mark_t mark  = { 0, 0, 0 };

    int implicit = (strcmp((char *)node->tag, YAML_DEFAULT_SEQUENCE_TAG) == 0);

    yaml_node_item_t *item;

    SEQUENCE_START_EVENT_INIT(event, anchor, node->tag, implicit,
            node->data.sequence.style, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) return 0;

    for (item = node->data.sequence.items.start;
            item < node->data.sequence.items.top; item ++) {
        if (!yaml_emitter_dump_node(emitter, *item)) return 0;
    }

    SEQUENCE_END_EVENT_INIT(event, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) return 0;

    return 1;
}

/*
 * Serialize a mapping.
 */

static int
yaml_emitter_dump_mapping(yaml_emitter_t *emitter, yaml_node_t *node,
        yaml_char_t *anchor)
{
    yaml_event_t event;
    yaml_mark_t mark  = { 0, 0, 0 };

    int implicit = (strcmp((char *)node->tag, YAML_DEFAULT_MAPPING_TAG) == 0);

    yaml_node_pair_t *pair;

    MAPPING_START_EVENT_INIT(event, anchor, node->tag, implicit,
            node->data.mapping.style, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) return 0;

    for (pair = node->data.mapping.pairs.start;
            pair < node->data.mapping.pairs.top; pair ++) {
        if (!yaml_emitter_dump_node(emitter, pair->key)) return 0;
        if (!yaml_emitter_dump_node(emitter, pair->value)) return 0;
    }

    MAPPING_END_EVENT_INIT(event, mark, mark);
    if (!yaml_emitter_emit(emitter, &event)) return 0;

    return 1;
}

