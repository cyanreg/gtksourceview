/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * gtksourceregion.c - GtkTextMark-based region utility
 * This file is part of GtkSourceView
 *
 * Copyright (C) 2002 Gustavo Giráldez <gustavo.giraldez@gmx.net>
 * Copyright (C) 2016 Sébastien Wilmet <swilmet@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "gtksourceregion.h"

/**
 * SECTION:region
 * @Short_description: Region utility
 * @Title: GtkSourceRegion
 * @See_also: #GtkTextBuffer
 *
 * A #GtkSourceRegion permits to store a group of subregions of a
 * #GtkTextBuffer. #GtkSourceRegion stores the subregions with pairs of
 * #GtkTextMark's, so the region is still valid after insertions and deletions
 * in the #GtkTextBuffer.
 *
 * The #GtkTextMark for the start of a subregion has a left gravity, while the
 * #GtkTextMark for the end of a subregion has a right gravity.
 */

/* With the gravities of the GtkTextMarks, it is possible for subregions to
 * become interlaced:
 * Buffer content:
 *   "hello world"
 * Add two subregions:
 *   "[hello] [world]"
 * Delete the space:
 *   "[hello][world]"
 * Undo:
 *   "[hello[ ]world]"
 *
 * FIXME: when iterating through the subregions, it should simplify them first.
 * I don't know if it's done (swilmet).
 */

#undef ENABLE_DEBUG
/*
#define ENABLE_DEBUG
*/

#ifdef ENABLE_DEBUG
#define DEBUG(x) (x)
#else
#define DEBUG(x)
#endif

typedef struct _GtkSourceRegionPrivate GtkSourceRegionPrivate;
typedef struct _Subregion Subregion;
typedef struct _GtkSourceRegionIterReal GtkSourceRegionIterReal;

struct _GtkSourceRegionPrivate
{
	/* Weak pointer to the buffer. */
	GtkTextBuffer *buffer;

	/* List of sorted 'Subregion*' */
	GList *subregions;

	guint32 timestamp;
};

struct _Subregion
{
	GtkTextMark *start;
	GtkTextMark *end;
};

struct _GtkSourceRegionIterReal
{
	GtkSourceRegion *region;
	guint32 region_timestamp;
	GList *subregions;
};

enum
{
	PROP_0,
	PROP_BUFFER,
	LAST_PROP
};

static GParamSpec *properties[LAST_PROP];

G_DEFINE_TYPE_WITH_PRIVATE (GtkSourceRegion, gtk_source_region, G_TYPE_OBJECT)

/* Find and return a subregion node which contains the given text
 * iter.  If left_side is TRUE, return the subregion which contains
 * the text iter or which is the leftmost; else return the rightmost
 * subregion.
 */
static GList *
find_nearest_subregion (GtkSourceRegion   *region,
			const GtkTextIter *iter,
			GList             *begin,
			gboolean           leftmost,
			gboolean           include_edges)
{
	GtkSourceRegionPrivate *priv = gtk_source_region_get_instance_private (region);
	GList *retval;
	GList *l;

	g_assert (iter != NULL);

	if (begin == NULL)
	{
		begin = priv->subregions;
	}

	if (begin != NULL)
	{
		retval = begin->prev;
	}
	else
	{
		retval = NULL;
	}

	for (l = begin; l != NULL; l = l->next)
	{
		GtkTextIter sr_iter;
		Subregion *sr = l->data;
		gint cmp;

		if (!leftmost)
		{
			gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_iter, sr->end);
			cmp = gtk_text_iter_compare (iter, &sr_iter);
			if (cmp < 0 || (cmp == 0 && include_edges))
			{
				retval = l;
				break;
			}

		}
		else
		{
			gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_iter, sr->start);
			cmp = gtk_text_iter_compare (iter, &sr_iter);
			if (cmp > 0 || (cmp == 0 && include_edges))
			{
				retval = l;
			}
			else
			{
				break;
			}
		}
	}

	return retval;
}

static void
gtk_source_region_get_property (GObject    *object,
				guint       prop_id,
				GValue     *value,
				GParamSpec *pspec)
{
	GtkSourceRegion *region = GTK_SOURCE_REGION (object);

	switch (prop_id)
	{
		case PROP_BUFFER:
			g_value_set_object (value, gtk_source_region_get_buffer (region));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_region_set_property (GObject      *object,
				guint         prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GtkSourceRegionPrivate *priv = gtk_source_region_get_instance_private (GTK_SOURCE_REGION (object));

	switch (prop_id)
	{
		case PROP_BUFFER:
			g_assert (priv->buffer == NULL);
			priv->buffer = g_value_get_object (value);
			g_object_add_weak_pointer (G_OBJECT (priv->buffer),
						   (gpointer *) &priv->buffer);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_region_dispose (GObject *object)
{
	GtkSourceRegionPrivate *priv = gtk_source_region_get_instance_private (GTK_SOURCE_REGION (object));

	while (priv->subregions != NULL)
	{
		Subregion *sr = priv->subregions->data;

		if (priv->buffer != NULL)
		{
			gtk_text_buffer_delete_mark (priv->buffer, sr->start);
			gtk_text_buffer_delete_mark (priv->buffer, sr->end);
		}

		g_slice_free (Subregion, sr);
		priv->subregions = g_list_delete_link (priv->subregions, priv->subregions);
	}

	if (priv->buffer != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->buffer),
					      (gpointer *) &priv->buffer);

		priv->buffer = NULL;
	}

	G_OBJECT_CLASS (gtk_source_region_parent_class)->dispose (object);
}

static void
gtk_source_region_class_init (GtkSourceRegionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gtk_source_region_get_property;
	object_class->set_property = gtk_source_region_set_property;
	object_class->dispose = gtk_source_region_dispose;

	properties[PROP_BUFFER] =
		g_param_spec_object ("buffer",
				     "Buffer",
				     "",
				     GTK_TYPE_TEXT_BUFFER,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
gtk_source_region_init (GtkSourceRegion *region)
{
}

/**
 * gtk_source_region_new:
 * @buffer: a #GtkTextBuffer.
 *
 * Returns: a new #GtkSourceRegion object for @buffer.
 * Since: 3.22
 */
GtkSourceRegion *
gtk_source_region_new (GtkTextBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_TEXT_BUFFER (buffer), NULL);

	return g_object_new (GTK_SOURCE_TYPE_REGION,
			     "buffer", buffer,
			     NULL);
}

/**
 * gtk_source_region_get_buffer:
 * @region: a #GtkSourceRegion.
 *
 * Returns: (transfer none) (nullable): the #GtkTextBuffer.
 * Since: 3.22
 */
GtkTextBuffer *
gtk_source_region_get_buffer (GtkSourceRegion *region)
{
	GtkSourceRegionPrivate *priv;

	g_return_val_if_fail (GTK_SOURCE_IS_REGION (region), NULL);

	priv = gtk_source_region_get_instance_private (region);
	return priv->buffer;
}

static void
gtk_source_region_clear_zero_length_subregions (GtkSourceRegion *region)
{
	GtkSourceRegionPrivate *priv = gtk_source_region_get_instance_private (region);
	GList *node;

	node = priv->subregions;
	while (node != NULL)
	{
		Subregion *sr = node->data;
		GtkTextIter start;
		GtkTextIter end;

		gtk_text_buffer_get_iter_at_mark (priv->buffer, &start, sr->start);
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &end, sr->end);

		if (gtk_text_iter_equal (&start, &end))
		{
			gtk_text_buffer_delete_mark (priv->buffer, sr->start);
			gtk_text_buffer_delete_mark (priv->buffer, sr->end);
			g_slice_free (Subregion, sr);

			if (node == priv->subregions)
			{
				priv->subregions = node = g_list_delete_link (node, node);
			}
			else
			{
				node = g_list_delete_link (node, node);
			}

			priv->timestamp++;
		}
		else
		{
			node = node->next;
		}
	}
}

/**
 * gtk_source_region_add:
 * @region: a #GtkSourceRegion.
 * @_start: the start of the subregion.
 * @_end: the end of the subregion.
 *
 * Adds the subregion delimited by @_start and @_end to @region.
 *
 * Since: 3.22
 */
void
gtk_source_region_add (GtkSourceRegion   *region,
		       const GtkTextIter *_start,
		       const GtkTextIter *_end)
{
	GtkSourceRegionPrivate *priv;
	GList *start_node;
	GList *end_node;
	GtkTextIter start;
	GtkTextIter end;

	g_return_if_fail (GTK_SOURCE_IS_REGION (region));
	g_return_if_fail (_start != NULL);
	g_return_if_fail (_end != NULL);

	priv = gtk_source_region_get_instance_private (region);

	if (priv->buffer == NULL)
	{
		return;
	}

	start = *_start;
	end = *_end;

	DEBUG (g_print ("---\n"));
	DEBUG (_gtk_source_region_debug_print (region));
	DEBUG (g_message ("region_add (%d, %d)",
			  gtk_text_iter_get_offset (&start),
			  gtk_text_iter_get_offset (&end)));

	gtk_text_iter_order (&start, &end);

	/* Don't add zero-length regions. */
	if (gtk_text_iter_equal (&start, &end))
	{
		return;
	}

	/* Find bounding subregions. */
	start_node = find_nearest_subregion (region, &start, NULL, FALSE, TRUE);
	end_node = find_nearest_subregion (region, &end, start_node, TRUE, TRUE);

	if (start_node == NULL || end_node == NULL || end_node == start_node->prev)
	{
		/* Create the new subregion. */
		Subregion *sr = g_slice_new0 (Subregion);
		sr->start = gtk_text_buffer_create_mark (priv->buffer, NULL, &start, TRUE);
		sr->end = gtk_text_buffer_create_mark (priv->buffer, NULL, &end, FALSE);

		if (start_node == NULL)
		{
			/* Append the new region. */
			priv->subregions = g_list_append (priv->subregions, sr);
		}
		else if (end_node == NULL)
		{
			/* Prepend the new region. */
			priv->subregions = g_list_prepend (priv->subregions, sr);
		}
		else
		{
			/* We are in the middle of two subregions. */
			priv->subregions = g_list_insert_before (priv->subregions, start_node, sr);
		}
	}
	else
	{
		GtkTextIter iter;
		Subregion *sr = start_node->data;

		if (start_node != end_node)
		{
			/* We need to merge some subregions. */
			GList *l = start_node->next;
			Subregion *q;

			gtk_text_buffer_delete_mark (priv->buffer, sr->end);

			while (l != end_node)
			{
				q = l->data;
				gtk_text_buffer_delete_mark (priv->buffer, q->start);
				gtk_text_buffer_delete_mark (priv->buffer, q->end);
				g_slice_free (Subregion, q);
				l = g_list_delete_link (l, l);
			}

			q = l->data;
			gtk_text_buffer_delete_mark (priv->buffer, q->start);
			sr->end = q->end;
			g_slice_free (Subregion, q);
			l = g_list_delete_link (l, l);
		}

		/* Now move marks if that action expands the region. */
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &iter, sr->start);
		if (gtk_text_iter_compare (&iter, &start) > 0)
		{
			gtk_text_buffer_move_mark (priv->buffer, sr->start, &start);
		}

		gtk_text_buffer_get_iter_at_mark (priv->buffer, &iter, sr->end);
		if (gtk_text_iter_compare (&iter, &end) < 0)
		{
			gtk_text_buffer_move_mark (priv->buffer, sr->end, &end);
		}
	}

	priv->timestamp++;

	DEBUG (_gtk_source_region_debug_print (region));
}

/**
 * gtk_source_region_subtract:
 * @region: a #GtkSourceRegion.
 * @_start: the start of the subregion.
 * @_end: the end of the subregion.
 *
 * Subtracts the subregion delimited by @_start and @_end from @region.
 *
 * Since: 3.22
 */
void
gtk_source_region_subtract (GtkSourceRegion   *region,
			    const GtkTextIter *_start,
			    const GtkTextIter *_end)
{
	GtkSourceRegionPrivate *priv;
	GList *start_node;
	GList *end_node;
	GList *node;
	GtkTextIter sr_start_iter;
	GtkTextIter sr_end_iter;
	gboolean done;
	gboolean start_is_outside;
	gboolean end_is_outside;
	Subregion *sr;
	GtkTextIter start;
	GtkTextIter end;

	g_return_if_fail (GTK_SOURCE_IS_REGION (region));
	g_return_if_fail (_start != NULL);
	g_return_if_fail (_end != NULL);

	priv = gtk_source_region_get_instance_private (region);

	if (priv->buffer == NULL)
	{
		return;
	}

	start = *_start;
	end = *_end;

	DEBUG (g_print ("---\n"));
	DEBUG (_gtk_source_region_debug_print (region));
	DEBUG (g_message ("region_substract (%d, %d)",
			  gtk_text_iter_get_offset (&start),
			  gtk_text_iter_get_offset (&end)));

	gtk_text_iter_order (&start, &end);

	/* Find bounding subregions. */
	start_node = find_nearest_subregion (region, &start, NULL, FALSE, FALSE);
	end_node = find_nearest_subregion (region, &end, start_node, TRUE, FALSE);

	/* Easy case first. */
	if (start_node == NULL || end_node == NULL || end_node == start_node->prev)
	{
		return;
	}

	/* Deal with the start point. */
	start_is_outside = end_is_outside = FALSE;

	sr = start_node->data;
	gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_start_iter, sr->start);
	gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_end_iter, sr->end);

	if (gtk_text_iter_in_range (&start, &sr_start_iter, &sr_end_iter) &&
	    !gtk_text_iter_equal (&start, &sr_start_iter))
	{
		/* The starting point is inside the first subregion. */
		if (gtk_text_iter_in_range (&end, &sr_start_iter, &sr_end_iter) &&
		    !gtk_text_iter_equal (&end, &sr_end_iter))
		{
			/* The ending point is also inside the first
			 * subregion: we need to split.
			 */
			Subregion *new_sr = g_slice_new0 (Subregion);
			new_sr->end = sr->end;
			new_sr->start = gtk_text_buffer_create_mark (priv->buffer,
								     NULL,
								     &end,
								     TRUE);

			start_node = g_list_insert_before (start_node, start_node->next, new_sr);

			sr->end = gtk_text_buffer_create_mark (priv->buffer,
							       NULL,
							       &start,
							       FALSE);

			/* No further processing needed. */
			DEBUG (g_message ("subregion splitted"));

			return;
		}
		else
		{
			/* The ending point is outside, so just move
			 * the end of the subregion to the starting point.
			 */
			gtk_text_buffer_move_mark (priv->buffer, sr->end, &start);
		}
	}
	else
	{
		/* The starting point is outside (and so to the left)
		 * of the first subregion.
		 */
		DEBUG (g_message ("start is outside"));

		start_is_outside = TRUE;
	}

	/* Deal with the end point. */
	if (start_node != end_node)
	{
		sr = end_node->data;
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_start_iter, sr->start);
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_end_iter, sr->end);
	}

	if (gtk_text_iter_in_range (&end, &sr_start_iter, &sr_end_iter) &&
	    !gtk_text_iter_equal (&end, &sr_end_iter))
	{
		/* Ending point is inside, move the start mark. */
		gtk_text_buffer_move_mark (priv->buffer, sr->start, &end);
	}
	else
	{
		end_is_outside = TRUE;
		DEBUG (g_message ("end is outside"));
	}

	/* Finally remove any intermediate subregions. */
	done = FALSE;
	node = start_node;

	while (!done)
	{
		if (node == end_node)
		{
			/* We are done, exit in the next iteration. */
			done = TRUE;
		}

		if ((node == start_node && !start_is_outside) ||
		    (node == end_node && !end_is_outside))
		{
			/* Skip starting or ending node. */
			node = node->next;
		}
		else
		{
			GList *l = node->next;
			sr = node->data;
			gtk_text_buffer_delete_mark (priv->buffer, sr->start);
			gtk_text_buffer_delete_mark (priv->buffer, sr->end);
			g_slice_free (Subregion, sr);
			priv->subregions = g_list_delete_link (priv->subregions, node);
			node = l;
		}
	}

	priv->timestamp++;

	DEBUG (_gtk_source_region_debug_print (region));

	/* Now get rid of empty subregions. */
	gtk_source_region_clear_zero_length_subregions (region);

	DEBUG (_gtk_source_region_debug_print (region));
}

/**
 * gtk_source_region_is_empty:
 * @region: (nullable): a #GtkSourceRegion, or %NULL.
 *
 * Returns whether the @region is empty. A %NULL @region is considered empty.
 *
 * Returns: whether the @region is empty.
 * Since: 3.22
 */
gboolean
gtk_source_region_is_empty (GtkSourceRegion *region)
{
	GtkSourceRegionIter region_iter;

	if (region == NULL)
	{
		return TRUE;
	}

	/* A #GtkSourceRegion can contain empty subregions. So checking the
	 * number of subregions is not sufficient.
	 * When calling gtk_source_region_add() with equal iters, the subregion
	 * is not added. But when a subregion becomes empty, due to text
	 * deletion, the subregion is not removed from the #GtkSourceRegion.
	 */

	gtk_source_region_get_start_region_iter (region, &region_iter);

	while (!gtk_source_region_iter_is_end (&region_iter))
	{
		GtkTextIter subregion_start;
		GtkTextIter subregion_end;

		if (!gtk_source_region_iter_get_subregion (&region_iter,
							   &subregion_start,
							   &subregion_end))
		{
			return TRUE;
		}

		if (!gtk_text_iter_equal (&subregion_start, &subregion_end))
		{
			return FALSE;
		}

		gtk_source_region_iter_next (&region_iter);
	}

	return TRUE;
}

/**
 * gtk_source_region_get_bounds:
 * @region: a #GtkSourceRegion.
 * @start: (out) (optional): iterator to initialize with the start of @region,
 *   or %NULL.
 * @end: (out) (optional): iterator to initialize with the end of @region,
 *   or %NULL.
 *
 * Gets the @start and @end bounds of the @region.
 *
 * Returns: %TRUE if @start and @end have been set successfully (if non-%NULL),
 *   or %FALSE if the @region is empty.
 * Since: 3.22
 */
gboolean
gtk_source_region_get_bounds (GtkSourceRegion *region,
			      GtkTextIter     *start,
			      GtkTextIter     *end)
{
	GtkSourceRegionPrivate *priv;

	g_return_val_if_fail (GTK_SOURCE_IS_REGION (region), FALSE);

	priv = gtk_source_region_get_instance_private (region);

	if (priv->buffer == NULL ||
	    gtk_source_region_is_empty (region))
	{
		return FALSE;
	}

	g_assert (priv->subregions != NULL);

	if (start != NULL)
	{
		Subregion *first_subregion = priv->subregions->data;
		gtk_text_buffer_get_iter_at_mark (priv->buffer, start, first_subregion->start);
	}

	if (end != NULL)
	{
		Subregion *last_subregion = g_list_last (priv->subregions)->data;
		gtk_text_buffer_get_iter_at_mark (priv->buffer, end, last_subregion->end);
	}

	return TRUE;
}

/**
 * gtk_source_region_intersect:
 * @region: a #GtkSourceRegion.
 * @_start: the start of the subregion.
 * @_end: the end of the subregion.
 *
 * Computes the intersection between @region and the subregion delimited by
 * @_start and @_end.
 *
 * Returns: (transfer full) (nullable): the intersection as a new
 *   #GtkSourceRegion.
 * Since: 3.22
 */
GtkSourceRegion *
gtk_source_region_intersect (GtkSourceRegion   *region,
			     const GtkTextIter *_start,
			     const GtkTextIter *_end)
{
	GtkSourceRegionPrivate *priv;
	GtkSourceRegion *new_region;
	GtkSourceRegionPrivate *new_priv;
	GList *start_node;
	GList *end_node;
	GList *node;
	GtkTextIter sr_start_iter;
	GtkTextIter sr_end_iter;
	Subregion *sr;
	Subregion *new_sr;
	gboolean done;
	GtkTextIter start;
	GtkTextIter end;

	g_return_val_if_fail (GTK_SOURCE_IS_REGION (region), NULL);
	g_return_val_if_fail (_start != NULL, NULL);
	g_return_val_if_fail (_end != NULL, NULL);

	priv = gtk_source_region_get_instance_private (region);

	if (priv->buffer == NULL)
	{
		return NULL;
	}

	start = *_start;
	end = *_end;

	gtk_text_iter_order (&start, &end);

	/* Find bounding subregions. */
	start_node = find_nearest_subregion (region, &start, NULL, FALSE, FALSE);
	end_node = find_nearest_subregion (region, &end, start_node, TRUE, FALSE);

	/* Easy case first. */
	if (start_node == NULL || end_node == NULL || end_node == start_node->prev)
	{
		return NULL;
	}

	new_region = gtk_source_region_new (priv->buffer);
	new_priv = gtk_source_region_get_instance_private (new_region);
	done = FALSE;

	sr = start_node->data;
	gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_start_iter, sr->start);
	gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_end_iter, sr->end);

	/* Starting node. */
	if (gtk_text_iter_in_range (&start, &sr_start_iter, &sr_end_iter))
	{
		new_sr = g_slice_new0 (Subregion);
		new_priv->subregions = g_list_prepend (new_priv->subregions, new_sr);

		new_sr->start = gtk_text_buffer_create_mark (new_priv->buffer,
							     NULL,
							     &start,
							     TRUE);

		if (start_node == end_node)
		{
			/* Things will finish shortly. */
			done = TRUE;
			if (gtk_text_iter_in_range (&end, &sr_start_iter, &sr_end_iter))
			{
				new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
									   NULL,
									   &end,
									   FALSE);
			}
			else
			{
				new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
									   NULL,
									   &sr_end_iter,
									   FALSE);
			}
		}
		else
		{
			new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
								   NULL,
								   &sr_end_iter,
								   FALSE);
		}

		node = start_node->next;
	}
	else
	{
		/* start should be the same as the subregion, so copy it in the
		 * loop.
		 */
		node = start_node;
	}

	if (!done)
	{
		while (node != end_node)
		{
			/* Copy intermediate subregions verbatim. */
			sr = node->data;
			gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_start_iter, sr->start);
			gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_end_iter, sr->end);

			new_sr = g_slice_new0 (Subregion);
			new_priv->subregions = g_list_prepend (new_priv->subregions, new_sr);

			new_sr->start = gtk_text_buffer_create_mark (new_priv->buffer,
								     NULL,
								     &sr_start_iter,
								     TRUE);

			new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
								   NULL,
								   &sr_end_iter,
								   FALSE);

			/* Next node. */
			node = node->next;
		}

		/* Ending node. */
		sr = node->data;
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_start_iter, sr->start);
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &sr_end_iter, sr->end);

		new_sr = g_slice_new0 (Subregion);
		new_priv->subregions = g_list_prepend (new_priv->subregions, new_sr);

		new_sr->start = gtk_text_buffer_create_mark (new_priv->buffer,
							     NULL,
							     &sr_start_iter,
							     TRUE);

		if (gtk_text_iter_in_range (&end, &sr_start_iter, &sr_end_iter))
		{
			new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
								   NULL,
								   &end,
								   FALSE);
		}
		else
		{
			new_sr->end = gtk_text_buffer_create_mark (new_priv->buffer,
								   NULL,
								   &sr_end_iter,
								   FALSE);
		}
	}

	new_priv->subregions = g_list_reverse (new_priv->subregions);
	return new_region;
}

static gboolean
check_iterator (GtkSourceRegionIterReal *real)
{
	GtkSourceRegionPrivate *priv;

	if (real->region == NULL)
	{
		goto invalid;
	}

	priv = gtk_source_region_get_instance_private (real->region);

	if (real->region_timestamp == priv->timestamp)
	{
		return TRUE;
	}

invalid:
	g_warning ("Invalid GtkSourceRegionIter: either the iterator is "
		   "uninitialized, or the region has been modified since the "
		   "iterator was created.");

	return FALSE;
}

/**
 * gtk_source_region_get_start_region_iter:
 * @region: a #GtkSourceRegion.
 * @iter: (out): iterator to initialize to the first subregion.
 *
 * Initializes a #GtkSourceRegionIter to the first subregion of @region. If
 * @region is empty, @iter will be initialized to the end iterator.
 *
 * Since: 3.22
 */
void
gtk_source_region_get_start_region_iter (GtkSourceRegion     *region,
					 GtkSourceRegionIter *iter)
{
	GtkSourceRegionPrivate *priv;
	GtkSourceRegionIterReal *real;

	g_return_if_fail (GTK_SOURCE_IS_REGION (region));
	g_return_if_fail (iter != NULL);

	priv = gtk_source_region_get_instance_private (region);
	real = (GtkSourceRegionIterReal *)iter;

	/* priv->subregions may be NULL, -> end iter */

	real->region = region;
	real->subregions = priv->subregions;
	real->region_timestamp = priv->timestamp;
}

/**
 * gtk_source_region_iter_is_end:
 * @iter: a #GtkSourceRegionIter.
 *
 * Returns: whether @iter is the end iterator.
 * Since: 3.22
 */
gboolean
gtk_source_region_iter_is_end (GtkSourceRegionIter *iter)
{
	GtkSourceRegionIterReal *real;

	g_return_val_if_fail (iter != NULL, FALSE);

	real = (GtkSourceRegionIterReal *)iter;
	g_return_val_if_fail (check_iterator (real), FALSE);

	return real->subregions == NULL;
}

/**
 * gtk_source_region_iter_next:
 * @iter: a #GtkSourceRegionIter.
 *
 * Moves @iter to the next subregion.
 *
 * Returns: %TRUE if @iter moved and is dereferenceable, or %FALSE if @iter has
 *   been set to the end iterator.
 * Since: 3.22
 */
gboolean
gtk_source_region_iter_next (GtkSourceRegionIter *iter)
{
	GtkSourceRegionIterReal *real;

	g_return_val_if_fail (iter != NULL, FALSE);

	real = (GtkSourceRegionIterReal *)iter;
	g_return_val_if_fail (check_iterator (real), FALSE);

	if (real->subregions != NULL)
	{
		real->subregions = real->subregions->next;
		return TRUE;
	}

	return FALSE;
}

/**
 * gtk_source_region_iter_get_subregion:
 * @iter: a #GtkSourceRegionIter.
 * @start: (out) (optional): iterator to initialize with the subregion start, or %NULL.
 * @end: (out) (optional): iterator to initialize with the subregion end, or %NULL.
 *
 * Gets the subregion at this iterator.
 *
 * Returns: %TRUE if @start and @end have been set successfully (if non-%NULL),
 *   or %FALSE if @iter is the end iterator or if the region is empty.
 * Since: 3.22
 */
gboolean
gtk_source_region_iter_get_subregion (GtkSourceRegionIter *iter,
				      GtkTextIter         *start,
				      GtkTextIter         *end)
{
	GtkSourceRegionIterReal *real;
	GtkSourceRegionPrivate *priv;
	Subregion *sr;

	g_return_val_if_fail (iter != NULL, FALSE);

	real = (GtkSourceRegionIterReal *)iter;
	g_return_val_if_fail (check_iterator (real), FALSE);

	if (real->subregions == NULL)
	{
		return FALSE;
	}

	priv = gtk_source_region_get_instance_private (real->region);

	if (priv->buffer == NULL)
	{
		return FALSE;
	}

	sr = real->subregions->data;
	g_return_val_if_fail (sr != NULL, FALSE);

	if (start != NULL)
	{
		gtk_text_buffer_get_iter_at_mark (priv->buffer, start, sr->start);
	}

	if (end != NULL)
	{
		gtk_text_buffer_get_iter_at_mark (priv->buffer, end, sr->end);
	}

	return TRUE;
}

void
_gtk_source_region_debug_print (GtkSourceRegion *region)
{
	GtkSourceRegionPrivate *priv;
	GList *l;

	g_return_if_fail (GTK_SOURCE_IS_REGION (region));

	priv = gtk_source_region_get_instance_private (region);

	if (priv->buffer == NULL)
	{
		return;
	}

	g_print ("Subregions: ");

	for (l = priv->subregions; l != NULL; l = l->next)
	{
		Subregion *sr = l->data;
		GtkTextIter start;
		GtkTextIter end;

		gtk_text_buffer_get_iter_at_mark (priv->buffer, &start, sr->start);
		gtk_text_buffer_get_iter_at_mark (priv->buffer, &end, sr->end);

		g_print ("%d-%d ",
			 gtk_text_iter_get_offset (&start),
			 gtk_text_iter_get_offset (&end));
	}
	g_print ("\n");
}