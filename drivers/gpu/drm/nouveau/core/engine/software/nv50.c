/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/namedb.h>
#include <core/handle.h>
#include <core/gpuobj.h>

#include <engine/software.h>
#include <engine/disp.h>

struct nv50_software_priv {
	struct nouveau_software base;
};

struct nv50_software_chan {
	struct nouveau_software_chan base;
};

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static int
nv50_software_mthd_dma_vblsem(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	struct nouveau_fifo_chan *fifo = (void *)nv_object(chan)->parent;
	struct nouveau_handle *handle;
	int ret = -EINVAL;

	handle = nouveau_namedb_get(nv_namedb(fifo), *(u32 *)args);
	if (!handle)
		return -ENOENT;

	if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
		struct nouveau_gpuobj *gpuobj = nv_gpuobj(handle->object);
		chan->base.vblank.ctxdma = gpuobj->node->offset >> 4;
		ret = 0;
	}
	nouveau_namedb_put(handle);
	return ret;
}

static int
nv50_software_mthd_vblsem_offset(struct nouveau_object *object, u32 mthd,
				 void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	chan->base.vblank.offset = *(u32 *)args;
	return 0;
}

static int
nv50_software_mthd_vblsem_value(struct nouveau_object *object, u32 mthd,
				void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	chan->base.vblank.value = *(u32 *)args;
	return 0;
}

static int
nv50_software_mthd_vblsem_release(struct nouveau_object *object, u32 mthd,
				  void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	struct nouveau_disp *disp = nouveau_disp(object);
	unsigned long flags;
	u32 crtc = *(u32 *)args;

	if (crtc > 1)
		return -EINVAL;

	disp->vblank.get(disp->vblank.data, crtc);

	spin_lock_irqsave(&disp->vblank.lock, flags);
	list_add(&chan->base.vblank.head, &disp->vblank.list);
	chan->base.vblank.crtc = crtc;
	spin_unlock_irqrestore(&disp->vblank.lock, flags);
	return 0;
}

static int
nv50_software_mthd_flip(struct nouveau_object *object, u32 mthd,
			void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	if (chan->base.flip)
		return chan->base.flip(chan->base.flip_data);
	return -EINVAL;
}

static struct nouveau_omthds
nv50_software_omthds[] = {
	{ 0x018c, nv50_software_mthd_dma_vblsem },
	{ 0x0400, nv50_software_mthd_vblsem_offset },
	{ 0x0404, nv50_software_mthd_vblsem_value },
	{ 0x0408, nv50_software_mthd_vblsem_release },
	{ 0x0500, nv50_software_mthd_flip },
	{}
};

static struct nouveau_oclass
nv50_software_sclass[] = {
	{ 0x506e, &nouveau_object_ofuncs, nv50_software_omthds },
	{}
};

/*******************************************************************************
 * software context
 ******************************************************************************/

static int
nv50_software_context_ctor(struct nouveau_object *parent,
			   struct nouveau_object *engine,
			   struct nouveau_oclass *oclass, void *data, u32 size,
			   struct nouveau_object **pobject)
{
	struct nv50_software_chan *chan;
	int ret;

	ret = nouveau_software_context_create(parent, engine, oclass, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->base.vblank.channel = nv_gpuobj(parent->parent)->addr >> 12;
	return 0;
}

static struct nouveau_oclass
nv50_software_cclass = {
	.handle = NV_ENGCTX(SW, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_software_context_ctor,
		.dtor = _nouveau_software_context_dtor,
		.init = _nouveau_software_context_init,
		.fini = _nouveau_software_context_fini,
	},
};

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

static int
nv50_software_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nv50_software_priv *priv;
	int ret;

	ret = nouveau_software_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nv50_software_cclass;
	nv_engine(priv)->sclass = nv50_software_sclass;
	nv_subdev(priv)->intr = nv04_software_intr;
	return 0;
}

struct nouveau_oclass
nv50_software_oclass = {
	.handle = NV_ENGINE(SW, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_software_ctor,
		.dtor = _nouveau_software_dtor,
		.init = _nouveau_software_init,
		.fini = _nouveau_software_fini,
	},
};
