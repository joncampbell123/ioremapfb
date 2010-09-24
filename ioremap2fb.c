/*
 * framebuffer driver for VBE 2.0 compliant graphic boards
 *
 * switching to graphics mode happens at boot time (while
 * running in real mode, see arch/i386/boot/video.S).
 *
 * (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <video/vga.h>
#include <asm/io.h>
#include <asm/mtrr.h>

#include "config.h"

#define MAX_FRAMEBUFFERS		16

static struct fb_info*			framebuffer[MAX_FRAMEBUFFERS] = {NULL};
static struct device_attribute		framebuffer_unreg_attr[MAX_FRAMEBUFFERS];

struct ioremap2fb_info {
	u32				pseudo_palette[256];
	struct pci_dev*			pci_dev;
	unsigned int			index;
	unsigned int			unreg_attr;
};

static struct platform_device*		ioremap2fb_device;

struct ioremapfb_info {
	u32			pseudo_palette[256];
};

#if 0
static unsigned long		fb_base=0,fb_size=0;
static int			fb_bpp=0,fb_stride=0,fb_width=0,fb_height=0;
static char*			fb_pci_dev=NULL;
static char*			fb_base_size=NULL;
static struct pci_dev*		pci_dev=NULL;
static struct fb_info*		framebuffer_info=NULL;
static struct ioremapfb_info*	framebuffer_priv=NULL;
static int			fb_reg=0;
static char*			fb_vram=NULL;
#endif

static struct platform_driver ioremap2fb_driver = {
	.driver = {
		.name = "ioremap2fb",
	},
};

/* --------------------------------------------------------------------- */

static int ioremap2fb_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp,
		struct fb_info *info)
{
	if (regno < 16) {
		u32 *pal = (u32*)info->pseudo_palette;

		if (info->var.bits_per_pixel == 16) {
			pal[regno] = (red & 0xF800) |
				((green & 0xFC00) >> 5) |
				((blue & 0xF800) >> 11);
		}
		else if (info->var.bits_per_pixel >= 24) {
			pal[regno] = (red >> 8) |
				((green >> 8) << 8) |
				((blue >> 8) << 16);
		}
	}

	return 0;
}

static struct fb_ops ioremap2fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= ioremap2fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

struct pci_dev *ioremap2fb_get_pci_dev(const char *s) {
	int domain=0,busno=0,slot=0,function=0;
	struct pci_bus *bus = NULL;
	struct pci_dev *dev = NULL;
	char *p = (char*)s;

	domain = simple_strtol(p,&p,0);
	if (*p++ != ':') return NULL;

	busno = simple_strtol(p,&p,0);
	if (*p++ != ':') return NULL;

	slot = simple_strtol(p,&p,0);
	if (*p != ':' && *p != '.') return NULL;
	p++;

	function = simple_strtol(p,&p,0);

	if ((bus = pci_find_bus(domain,busno)) != NULL) {
		printk(KERN_INFO "got PCI domain %d bus %d\n",domain,busno);
		if ((dev = pci_get_slot(bus,PCI_DEVFN(slot,function))) != NULL) {
			printk(KERN_INFO "got pci dev %d,%d\n",slot,function);
			/* make sure it's VGA-class so userspace doesn't make their IDE controller the framebuffer device */
			if ((dev->class & 0xFF0000) != 0x030000) {
				pci_dev_put(dev);
				printk(KERN_ERR "   nope, not VGA class 0x%08X\n",dev->class);
			}
			else {
				return dev;
			}
		}
	}

	return NULL;
}

#if 0
static int __init setup_fb(void) {
	int ret;

	pci_enable_device(pci_dev);

	fb_vram = ioremap(fb_base,fb_size);
	if (fb_vram == NULL) {
		printk(KERN_ERR "Cannot mmap vram\n");
		return -ENOMEM;
	}

	framebuffer_info = framebuffer_alloc(sizeof(struct ioremapfb_info), &pci_dev->dev);
	if (!framebuffer_info) {
		printk(KERN_ERR "failed to register framebuffer\n");
		return -ENOMEM;
	}
	framebuffer_priv = framebuffer_info->par;
	framebuffer_info->pseudo_palette = framebuffer_priv->pseudo_palette;

	pci_set_drvdata(pci_dev,framebuffer_info);

	ioremapfb_var.xres = ioremapfb_var.xres_virtual = fb_width;
	ioremapfb_var.yres = ioremapfb_var.yres_virtual = fb_height;
	ioremapfb_var.bits_per_pixel = fb_bpp;
	ioremapfb_fix.line_length = fb_stride;
	ioremapfb_fix.visual = FB_VISUAL_TRUECOLOR;
	ioremapfb_fix.smem_start = fb_base;
	ioremapfb_fix.smem_len = fb_size;

	ioremapfb_var.pixclock = 10000000 / fb_width * 1000 / fb_height;
	ioremapfb_var.left_margin = (fb_width / 8) & 0xF8;
	ioremapfb_var.right_margin = (fb_width / 8) & 0xF8;

	if (fb_bpp == 16) {
		ioremapfb_var.red.offset = 11;
		ioremapfb_var.red.length = 5;
		ioremapfb_var.green.offset = 5;
		ioremapfb_var.green.length = 6;
		ioremapfb_var.blue.offset = 0;
		ioremapfb_var.blue.length = 5;
	}

	framebuffer_info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_DISABLED;
	framebuffer_info->fbops = &ioremapfb_ops;
	framebuffer_info->screen_base = fb_vram;
	framebuffer_info->screen_size = fb_size;
	framebuffer_info->fix = ioremapfb_fix;
	framebuffer_info->var = ioremapfb_var;

	if (fb_alloc_cmap(&framebuffer_info->cmap, 256, 0) < 0) {
		printk(KERN_ERR "Cannot alloc cmap\n");
		return 1;
	}

	if ((ret=register_framebuffer(framebuffer_info)) < 0) {
		printk(KERN_ERR "Cannot register framebuffer code %d\n",ret);
		fb_dealloc_cmap(&framebuffer_info->cmap);
		return ret;
	}

	fb_reg = 1;
	return 0;
}
#endif

#if 0
static void __exit unsetup_fb(void) {
	if (framebuffer_info) {
		if (fb_vram) iounmap(fb_vram);
		fb_vram = NULL;
		framebuffer_priv = NULL;
		if (fb_reg) unregister_framebuffer(framebuffer_info);
		framebuffer_release(framebuffer_info);
		framebuffer_info = NULL;
	}
}
#endif

static unsigned int count_active(void) {
	unsigned int i,count=0;

	for (i=0;i < MAX_FRAMEBUFFERS;i++)
		if (framebuffer[i] != NULL)
			count++;

	return count;
}

static void force_remove_fb(unsigned int idx) {
	struct ioremap2fb_info *pf;
	struct fb_info *f;

	BUG_ON(idx >= MAX_FRAMEBUFFERS);
	if ((f=framebuffer[idx]) == NULL) return;
	framebuffer[idx] = 0;
	pf = f->par;

	if (pf->unreg_attr)
		device_remove_file(f->dev,&framebuffer_unreg_attr[idx]);

	unregister_framebuffer(f);
	release_mem_region(f->fix.smem_start,f->fix.smem_len);
	iounmap(f->screen_base);
	pci_dev_put(pf->pci_dev);
	fb_dealloc_cmap(&f->cmap);
	framebuffer_release(f);
}

static void unregister_me_cb(struct device *dev) {
	struct ioremap2fb_info *p = dev->platform_data;
	BUG_ON(p->index >= MAX_FRAMEBUFFERS);
	force_remove_fb(p->index);
}

/* because of the way we're registered, we're guaranteed that dev is whatever framebuffer device we were allocated with */
static ssize_t unregister_me_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	/* apparently an attibute cannot remove itself */
	device_schedule_callback(dev,unregister_me_cb);
	return count;
}

static DEVICE_ATTR(unregister_me, S_IWUSR, NULL, unregister_me_store);

static ssize_t show_register_fb(struct device_driver *dev,char *buf) {
	return snprintf(buf, PAGE_SIZE, "%u\n", count_active());
}

struct new_fb_info {
	char			name[16+1];
	struct pci_dev*		pci_dev;
	unsigned long		fb_base;
	unsigned long		fb_size;
	unsigned int		pitch;
	unsigned int		xres,yres;
	unsigned int		red_shift;
	unsigned int		blue_shift;
	unsigned int		green_shift;
	unsigned int		bits_per_pixel;
};

static void dispose_new_fb_info(struct new_fb_info *f) {
	memset(f,0,sizeof(*f));
}

static int find_open_slot(void) {
	int i;
	for (i=0;i < MAX_FRAMEBUFFERS;i++) {
		if (framebuffer[i] == NULL)
			return i;
	}

	return -1;
}

static ssize_t store_register_fb(struct device_driver *dev,const char *buf,size_t count) {
	const char *fence = buf + count;
	struct new_fb_info *nfb;
	struct fb_info *f = NULL;
	void *mmap = NULL;
	int req_mem = 0;
	int ret = count;
	int index = -1;
	int reg_fb = 0;
	int i;

	if ((nfb = kmalloc(sizeof(struct new_fb_info),GFP_KERNEL)) == NULL)
		return -ENOMEM;

	memset(nfb,0,sizeof(*nfb));
	/* the string is a collection of name=value pairs separated by commas */
	while (buf < fence && *buf != 0 && *buf != '\n') {
		const char *name = buf,*value; /* the string is const char, we can't just write zeros over it! */
		size_t l_name,l_value;

		if (*buf == ' ') {
			buf++;
			continue;
		}

		while (buf < fence && *buf != 0 && *buf != '=') buf++;
		l_name = (size_t)(buf - name);
		if (*buf == '=') buf++;
		value = buf;

		while (buf < fence && *buf != 0 && *buf != ',' && *buf != '\n') buf++;
		l_value = (size_t)(buf - value);

		if (*buf == ',') buf++;

		if (l_name == 0) continue;

		if (!strncmp(name,"name",l_name)) {
			if (l_value > 0)
				memcpy(nfb->name, value, min(l_value, sizeof(nfb->name) - 1));
		}
		else if (!strncmp(name,"pci",l_name)) {
			if (nfb->pci_dev != NULL)
				{ ret = -EINVAL; break; }
			if ((nfb->pci_dev = ioremap2fb_get_pci_dev(value)) == NULL)
				{ ret = -ENODEV; break; }
		}
		else if (!strncmp(name,"fb_base",l_name)) {
			nfb->fb_base = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"fb_size",l_name)) {
			nfb->fb_size = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"bpp",l_name)) { /* allow a simple shorthand */
			long x = simple_strtol(value,NULL,0);
			if (x == 16) { /* probably 5:6:5 */
				nfb->bits_per_pixel = 16;
				nfb->red_shift = 11;
				nfb->green_shift = 5;
				nfb->blue_shift = 0;
			}
			else if (x == 24) {
				nfb->bits_per_pixel = 24;
				nfb->red_shift = 16;
				nfb->green_shift = 8;
				nfb->blue_shift = 0;
			}
			else if (x == 32) {
				nfb->bits_per_pixel = 32;
				nfb->red_shift = 16;
				nfb->green_shift = 8;
				nfb->blue_shift = 0;
			}
		}
		else if (!strncmp(name,"xres",l_name)) {
			nfb->xres = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"yres",l_name)) {
			nfb->yres = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"bits",l_name)) {
			nfb->bits_per_pixel = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"red",l_name)) {
			nfb->red_shift = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"green",l_name)) {
			nfb->green_shift = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"blue",l_name)) {
			nfb->blue_shift = simple_strtol(value,NULL,0);
		}
		else if (!strncmp(name,"pitch",l_name)) {
			nfb->pitch = simple_strtol(value,NULL,0);
		}
	}

	if (nfb->bits_per_pixel > 0 && nfb->xres > 0 && nfb->pitch == 0)
		nfb->pitch = (((nfb->bits_per_pixel/8) * nfb->xres) + 4) & (~7);

	if (nfb->pci_dev == NULL || nfb->bits_per_pixel == 0 || nfb->xres < 320 || nfb->yres < 100 ||
		(nfb->fb_base+nfb->fb_size-1) < nfb->fb_base || nfb->pitch < nfb->xres ||
		nfb->pitch > (nfb->xres * 5)) {/* also check for base+size overflow */
		printk(KERN_DEBUG "Invalid fb params pitch=%u xres=%u yres=%u\n",nfb->pitch,nfb->xres,nfb->yres);
		ret = -EINVAL;
	}

	if (ret >= 0) {
		BUG_ON(nfb->pci_dev == NULL);
		/* if the range given is not within the PCI device, it's a failure */
		for (i=0;(nfb->fb_base == 0 || nfb->fb_size == 0) && i < DEVICE_COUNT_RESOURCE;i++) { /* then find the largest prefetchable address, it's very likely video RAM */
			struct resource *r = &nfb->pci_dev->resource[i];
			if (	(r->flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM ||
				!(r->flags & IORESOURCE_PREFETCH) ||
				(r->flags & IORESOURCE_READONLY) ||
				(r->flags & IORESOURCE_DISABLED))
				continue;

			nfb->fb_base = r->start;
			nfb->fb_size = (r->end - r->start) + 1;
		}

		if (nfb->fb_base == 0 || nfb->fb_size == 0) {
			printk(KERN_DEBUG "PCI dev given without range and I can't autodetect VRAM resource\n");
			ret = -ENODEV;
		}
	}

	if (ret >= 0) {
		int found = 0;
		/* make sure the range given is entirely contained within one of the resources */
		for (i=0;i < DEVICE_COUNT_RESOURCE && !found;i++) {
			struct resource *r = &nfb->pci_dev->resource[i];

			if ((r->flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM)
				continue;

			if (nfb->fb_base >= r->start && (nfb->fb_base+nfb->fb_size-1) <= r->end)
				found = 1;
		}

		if (!found) {
			printk(KERN_DEBUG "framebuffer address is not within range of any PCI resource\n");
			ret = -EINVAL;
		}
	}

	if (ret >= 0) {
		if ((nfb->pitch * nfb->yres) > nfb->fb_size) {
			printk(KERN_DEBUG "framebuffer range given is not large enough for specified buffer, %lu vs %lu\n",
				(long)(nfb->pitch * nfb->yres),(long)nfb->fb_size);
			ret = -EINVAL;
		}
	}

	if (ret >= 0) {
		index = find_open_slot();
		if (index < 0) ret = -ENOSPC;
	}

	if (ret >= 0) {
		if (pci_enable_device(nfb->pci_dev))
			ret = -EINVAL;
	}

	if (ret >= 0) {	
/*		printk(KERN_DEBUG "framebuffer_alloc\n"); */
		f = framebuffer_alloc(sizeof(struct ioremap2fb_info), &nfb->pci_dev->dev);
		if (f == NULL) {
			ret = -ENOMEM;
		}
		else {
			unsigned int htotal,vtotal;
			const unsigned int rate = 60;	/* assume 60Hz */
			struct fb_fix_screeninfo *fix = &f->fix;
			struct fb_var_screeninfo *var = &f->var;
			struct ioremap2fb_info *priv = f->par;
/*			printk(KERN_DEBUG "framebuffer setup\n"); */
			f->pseudo_palette = priv->pseudo_palette;
			priv->pci_dev = nfb->pci_dev;
			priv->index = index;
			priv->unreg_attr = 0;
			var->xres = nfb->xres;
			var->yres = nfb->yres;
			var->xres_virtual = nfb->pitch / ((nfb->bits_per_pixel+7)/8);
			var->yres_virtual = nfb->fb_size / nfb->pitch;
			var->bits_per_pixel = nfb->bits_per_pixel;
			fix->line_length = nfb->pitch;
			fix->visual = FB_VISUAL_TRUECOLOR;
			fix->smem_start = nfb->fb_base;
			fix->smem_len = nfb->fb_size;
			var->left_margin = (nfb->xres / 8);
			var->right_margin = (nfb->xres / 8);
			var->upper_margin = (nfb->yres / 8);
			var->lower_margin = (nfb->yres / 8);
			var->hsync_len = 64;
			var->vsync_len = 2;
			var->red.offset = nfb->red_shift;
			var->green.offset = nfb->green_shift;
			var->blue.offset = nfb->blue_shift;
			if (nfb->bits_per_pixel == 16) {
				if (nfb->red_shift == (5+6)) {
					var->red.length = 5;
					var->green.length = 6;
					var->blue.length = 5;
				}
				else {
					var->red.length = 5;
					var->green.length = 5;
					var->blue.length = 5;
				}
			}
			else {
				var->red.length =
				var->green.length =
				var->blue.length = 8;
			}
			htotal = var->xres + var->left_margin + var->hsync_len + var->right_margin;
			vtotal = var->yres + var->upper_margin + var->vsync_len + var->lower_margin;
			var->pixclock = ((1000000000 / htotal) * 1000) / vtotal / rate;
			f->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_DISABLED;
			f->fbops = &ioremap2fb_ops;
			f->screen_size = nfb->fb_size;
			f->screen_base = NULL;
			strncpy(fix->id,nfb->name,sizeof(fix->id)-1);
			fix->id[sizeof(fix->id)-1] = 0;

/*			printk(KERN_DEBUG "request_mem_region\n"); */
			if (request_mem_region(nfb->fb_base,nfb->fb_size,"ioremap2fb framebuffer")) {
				req_mem = 1;
			}
			else {
/*				printk(KERN_DEBUG "cannot claim memory region\n"); */
				ret = -ENOMEM;
			}

/*			printk(KERN_DEBUG "ioremap\n"); */
			if (ret >= 0 && (mmap = ioremap(nfb->fb_base,nfb->fb_size)) == NULL) {
/*				printk(KERN_DEBUG "cannot ioremap()\n"); */
				ret = -ENOMEM;
			}

			f->screen_base = mmap;
/*			printk(KERN_DEBUG "alloc cmap\n"); */
			if (ret >= 0 && fb_alloc_cmap(&f->cmap,256,0) < 0)
				ret = -ENOMEM;

/*			printk(KERN_DEBUG "reg framebuffer\n"); */
			if (ret >= 0) {
				if (register_framebuffer(f)) {
					printk(KERN_DEBUG "failed to register framebuffer\n");
					ret = -ENOMEM;
				}
				else {
					reg_fb = 1;
				}
			}
		}

		if (ret < 0) {
			fb_dealloc_cmap(&f->cmap);
			framebuffer_release(f);
			f = NULL;
		}
	}

#if 0
	/* DEBUG: make it fail */
	if (1 && ret >= 0)
		ret = -ENOMEM;
#endif

	/* if success, then store the pointer, and hold onto the PCI device */
	if (ret >= 0) {
		struct ioremap2fb_info *priv = f->par;

		BUG_ON(reg_fb == 0);
		BUG_ON(req_mem == 0);
		BUG_ON(mmap == NULL);
		BUG_ON(nfb->pci_dev == NULL);
		framebuffer[index] = f;

/*		printk(KERN_DEBUG "%d\n",index); */
		f->dev->platform_data = (void*)priv;
		framebuffer_unreg_attr[index] = dev_attr_unregister_me;
		if (!device_create_file(f->dev,&framebuffer_unreg_attr[index]))
			priv->unreg_attr = 1;
	}

	if (ret < 0 && reg_fb) {
/*		printk(KERN_DEBUG "Unregister framebuffer\n"); */
		BUG_ON(f == NULL);
		unregister_framebuffer(f);
		fb_dealloc_cmap(&f->cmap);
		framebuffer_release(f);
		f = NULL;
	}
	if (ret < 0 && req_mem) {
/*		printk(KERN_DEBUG "Releasing mem region\n"); */
		release_mem_region(nfb->fb_base,nfb->fb_size);
		req_mem = 0;
	}
	if (ret < 0 && mmap != NULL) {
/*		printk(KERN_DEBUG "iounmap\n"); */
		iounmap(mmap);
		mmap = NULL;
	}
	if (ret < 0 && nfb->pci_dev != NULL) {
/*		printk(KERN_DEBUG "pci dev put\n"); */
		pci_dev_put(nfb->pci_dev);
		nfb->pci_dev = NULL;
	}

	dispose_new_fb_info(nfb);
	kfree(nfb);
	return ret;
}

static DRIVER_ATTR(register_fb,S_IRUSR | S_IWUSR,show_register_fb,store_register_fb);

static int __init ioremapfb_init(void)
{
	int err = 0;

	if ((err = platform_driver_register(&ioremap2fb_driver)) == 0) {
		if ((ioremap2fb_device = platform_device_alloc("ioremap2fb", 0)) != NULL) {
			err = platform_device_add(ioremap2fb_device);
		}
		else {
			err = -ENOMEM;
		}

		if (err) {
			platform_device_put(ioremap2fb_device);
			platform_driver_unregister(&ioremap2fb_driver);
			return err;
		}

		if (driver_create_file(&ioremap2fb_driver.driver,&driver_attr_register_fb))
			printk(KERN_WARNING "ioremap2fb: cannot register attribute\n");
	}

	return err;
}

static void __exit ioremapfb_exit(void)
{
	int i;

	driver_remove_file(&ioremap2fb_driver.driver,&driver_attr_register_fb);
	platform_device_unregister(ioremap2fb_device);
	platform_driver_unregister(&ioremap2fb_driver);

	for (i=0;i < MAX_FRAMEBUFFERS;i++)
		force_remove_fb(i);
}

module_init(ioremapfb_init);
module_exit(ioremapfb_exit);

MODULE_AUTHOR("Jonathan Campbell");
MODULE_DESCRIPTION("Dumb framebuffer for PCI video devices already initialized " IOREMAPFB_BUILD_BANNER);
MODULE_LICENSE("GPL");

