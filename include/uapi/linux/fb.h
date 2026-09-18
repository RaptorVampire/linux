/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * fb.h - frame buffer userspace ABI definitions
 *
 * This header defines the ABI between the kernel framebuffer subsystem
 * (drivers/video/fbdev/*) and userspace consumers (fbcon, X, Wayland, SDL,
 * fbset, fbtools, ...). All numeric values, struct layouts, ioctl
 * encodings, and field ordering are ABI-stable and MUST NOT change.
 *
 * Historical notes:
 *   - The ioctl magic 'F' is 0x46, and many ioctls predate the modern
 *     _IO* encoding scheme, so they are expressed as raw hex 0x46xx.
 *     These raw values MUST be preserved verbatim.
 *   - Several structures contain `unsigned long` and pointer fields,
 *     which have different sizes on 32-bit vs 64-bit. The kernel
 *     provides compat translation (fb_fix_screeninfo32, etc.) for
 *     32-bit userspace running on 64-bit kernels. Do not "clean up"
 *     these fields to fixed-width types; that would break 32-bit ABI.
 */
#ifndef _UAPI_LINUX_FB_H
#define _UAPI_LINUX_FB_H

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/vesa.h>

/* ======================================================================
 * Frame buffer limits
 * ====================================================================== */

#define FB_MAX	32	/* sufficient for now */

/* ======================================================================
 * ioctl definitions
 *
 * The magic 0x46 is 'F'. Ioctls are listed in numeric order with the
 * corresponding arg type.
 * ====================================================================== */

#define FBIOGET_VSCREENINFO	0x4600	/* arg: struct fb_var_screeninfo * */
#define FBIOPUT_VSCREENINFO	0x4601	/* arg: struct fb_var_screeninfo * */
#define FBIOGET_FSCREENINFO	0x4602	/* arg: struct fb_fix_screeninfo * */
#define FBIOGETCMAP		0x4604	/* arg: struct fb_cmap * */
#define FBIOPUTCMAP		0x4605	/* arg: struct fb_cmap * */
#define FBIOPAN_DISPLAY		0x4606	/* arg: struct fb_var_screeninfo * */
/*
 * FBIO_CURSOR is only exposed to userspace. The kernel internally
 * routes the same command number through its own driver-facing
 * implementation, so guarding it here avoids redefinition clashes.
 */
#ifndef __KERNEL__
#define FBIO_CURSOR		_IOWR('F', 0x08, struct fb_cursor)
#endif
/* 0x4607-0x460B are defined below */
/* #define FBIOGET_MONITORSPEC	0x460C */
/* #define FBIOPUT_MONITORSPEC	0x460D */
/* #define FBIOSWITCH_MONIBIT	0x460E */
#define FBIOGET_CON2FBMAP	0x460F	/* arg: struct fb_con2fbmap * */
#define FBIOPUT_CON2FBMAP	0x4610	/* arg: struct fb_con2fbmap * */
#define FBIOBLANK		0x4611	/* arg: 0 or vesa level + 1 */
#define FBIOGET_VBLANK		_IOR('F', 0x12, struct fb_vblank)
					/* arg: struct fb_vblank * */
#define FBIO_ALLOC		0x4613	/* arg: int size */
#define FBIO_FREE		0x4614	/* arg: int offset */
#define FBIOGET_GLYPH		0x4615
#define FBIOGET_HWCINFO		0x4616
#define FBIOPUT_MODEINFO	0x4617
#define FBIOGET_DISPINFO	0x4618
#define FBIO_WAITFORVSYNC	_IOW('F', 0x20, __u32)

/* ======================================================================
 * fb_fix_screeninfo.type values
 * ====================================================================== */

#define FB_TYPE_PACKED_PIXELS		0	/* Packed Pixels		*/
#define FB_TYPE_PLANES			1	/* Non interleaved planes	*/
#define FB_TYPE_INTERLEAVED_PLANES	2	/* Interleaved planes		*/
#define FB_TYPE_TEXT			3	/* Text/attributes		*/
#define FB_TYPE_VGA_PLANES		4	/* EGA/VGA planes		*/
#define FB_TYPE_FOURCC			5	/* Type identified by a V4L2 FOURCC */

/* ======================================================================
 * fb_fix_screeninfo.type_aux values (when type == FB_TYPE_TEXT)
 * ====================================================================== */

#define FB_AUX_TEXT_MDA		0	/* Monochrome text */
#define FB_AUX_TEXT_CGA		1	/* CGA/EGA/VGA Color text */
#define FB_AUX_TEXT_S3_MMIO	2	/* S3 MMIO fasttext */
#define FB_AUX_TEXT_MGA_STEP16	3	/* MGA Millenium I: text, attr, 14 reserved bytes */
#define FB_AUX_TEXT_MGA_STEP8	4	/* other MGAs:      text, attr,  6 reserved bytes */
#define FB_AUX_TEXT_SVGA_GROUP	8	/* 8-15: SVGA tileblit compatible modes */
#define FB_AUX_TEXT_SVGA_MASK	7	/* lower three bits says step */
#define FB_AUX_TEXT_SVGA_STEP2	8	/* SVGA text mode:  text, attr */
#define FB_AUX_TEXT_SVGA_STEP4	9	/* SVGA text mode:  text, attr,  2 reserved bytes */
#define FB_AUX_TEXT_SVGA_STEP8	10	/* SVGA text mode:  text, attr,  6 reserved bytes */
#define FB_AUX_TEXT_SVGA_STEP16	11	/* SVGA text mode:  text, attr, 14 reserved bytes */
#define FB_AUX_TEXT_SVGA_LAST	15	/* reserved up to 15 */

/* ======================================================================
 * fb_fix_screeninfo.type_aux values (when type == FB_TYPE_VGA_PLANES)
 * ====================================================================== */

#define FB_AUX_VGA_PLANES_VGA4	0	/* 16 color planes (EGA/VGA)	*/
#define FB_AUX_VGA_PLANES_CFB4	1	/* CFB4 in planes (VGA)		*/
#define FB_AUX_VGA_PLANES_CFB8	2	/* CFB8 in planes (VGA)		*/

/* ======================================================================
 * fb_fix_screeninfo.visual / fb_var_screeninfo.visual values
 * ====================================================================== */

#define FB_VISUAL_MONO01		0	/* Monochr. 1=Black 0=White */
#define FB_VISUAL_MONO10		1	/* Monochr. 1=White 0=Black */
#define FB_VISUAL_TRUECOLOR		2	/* True color			*/
#define FB_VISUAL_PSEUDOCOLOR		3	/* Pseudo color (like atari)	*/
#define FB_VISUAL_DIRECTCOLOR		4	/* Direct color			*/
#define FB_VISUAL_STATIC_PSEUDOCOLOR	5	/* Pseudo color readonly	*/
#define FB_VISUAL_FOURCC		6	/* Visual identified by a V4L2 FOURCC */

/* ======================================================================
 * fb_fix_screeninfo.accel values
 *
 * NOTE: The trailing comments map each value to a chip family for
 * documentation only; the numeric values are ABI and must not change.
 *
 * WARNING: There is a known historical collision between
 *   FB_ACCEL_TRIDENT_BLADEXP  and  FB_ACCEL_CIRRUS_ALPINE
 * (both are 53). This is a genuine ABI-preserved artifact and MUST NOT
 * be "fixed" — userspace tools key off these exact numeric values.
 * ====================================================================== */

#define FB_ACCEL_NONE			0	/* no hardware accelerator	*/
#define FB_ACCEL_ATARIBLITT		1	/* Atari Blitter		*/
#define FB_ACCEL_AMIGABLITT		2	/* Amiga Blitter		*/
#define FB_ACCEL_S3_TRIO64		3	/* Cybervision64 (S3 Trio64)	*/
#define FB_ACCEL_NCR_77C32BLT		4	/* RetinaZ3 (NCR 77C32BLT)	*/
#define FB_ACCEL_S3_VIRGE		5	/* Cybervision64/3D (S3 ViRGE)	*/
#define FB_ACCEL_ATI_MACH64GX		6	/* ATI Mach 64GX family		*/
#define FB_ACCEL_DEC_TGA		7	/* DEC 21030 TGA		*/
#define FB_ACCEL_ATI_MACH64CT		8	/* ATI Mach 64CT family		*/
#define FB_ACCEL_ATI_MACH64VT		9	/* ATI Mach 64CT family VT class */
#define FB_ACCEL_ATI_MACH64GT		10	/* ATI Mach 64CT family GT class */
#define FB_ACCEL_SUN_CREATOR		11	/* Sun Creator/Creator3D	*/
#define FB_ACCEL_SUN_CGSIX		12	/* Sun cg6			*/
#define FB_ACCEL_SUN_LEO		13	/* Sun leo/zx			*/
#define FB_ACCEL_IMS_TWINTURBO		14	/* IMS Twin Turbo		*/
#define FB_ACCEL_3DLABS_PERMEDIA2	15	/* 3Dlabs Permedia 2		*/
#define FB_ACCEL_MATROX_MGA2064W	16	/* Matrox MGA2064W (Millenium)	*/
#define FB_ACCEL_MATROX_MGA1064SG	17	/* Matrox MGA1064SG (Mystique)	*/
#define FB_ACCEL_MATROX_MGA2164W	18	/* Matrox MGA2164W (Millenium II) */
#define FB_ACCEL_MATROX_MGA2164W_AGP	19	/* Matrox MGA2164W (Millenium II) */
#define FB_ACCEL_MATROX_MGAG100		20	/* Matrox G100 (Productiva G100) */
#define FB_ACCEL_MATROX_MGAG200		21	/* Matrox G200 (Myst, Mill, ...) */
#define FB_ACCEL_SUN_CG14		22	/* Sun cgfourteen		*/
#define FB_ACCEL_SUN_BWTWO		23	/* Sun bwtwo			*/
#define FB_ACCEL_SUN_CGTHREE		24	/* Sun cgthree			*/
#define FB_ACCEL_SUN_TCX		25	/* Sun tcx			*/
#define FB_ACCEL_MATROX_MGAG400		26	/* Matrox G400			*/
#define FB_ACCEL_NV3			27	/* nVidia RIVA 128		*/
#define FB_ACCEL_NV4			28	/* nVidia RIVA TNT		*/
#define FB_ACCEL_NV5			29	/* nVidia RIVA TNT2		*/
#define FB_ACCEL_CT_6555x		30	/* C&T 6555x			*/
#define FB_ACCEL_3DFX_BANSHEE		31	/* 3Dfx Banshee			*/
#define FB_ACCEL_ATI_RAGE128		32	/* ATI Rage128 family		*/
#define FB_ACCEL_IGS_CYBER2000		33	/* CyberPro 2000		*/
#define FB_ACCEL_IGS_CYBER2010		34	/* CyberPro 2010		*/
#define FB_ACCEL_IGS_CYBER5000		35	/* CyberPro 5000		*/
#define FB_ACCEL_SIS_GLAMOUR		36	/* SiS 300/630/540		*/
#define FB_ACCEL_3DLABS_PERMEDIA3	37	/* 3Dlabs Permedia 3		*/
#define FB_ACCEL_ATI_RADEON		38	/* ATI Radeon family		*/
#define FB_ACCEL_I810			39	/* Intel 810/815		*/
#define FB_ACCEL_SIS_GLAMOUR_2		40	/* SiS 315, 650, 740		*/
#define FB_ACCEL_SIS_XABRE		41	/* SiS 330 ("Xabre")		*/
#define FB_ACCEL_I830			42	/* Intel 830M/845G/85x/865G	*/
#define FB_ACCEL_NV_10			43	/* nVidia Arch 10		*/
#define FB_ACCEL_NV_20			44	/* nVidia Arch 20		*/
#define FB_ACCEL_NV_30			45	/* nVidia Arch 30		*/
#define FB_ACCEL_NV_40			46	/* nVidia Arch 40		*/
#define FB_ACCEL_XGI_VOLARI_V		47	/* XGI Volari V3XT, V5, V8	*/
#define FB_ACCEL_XGI_VOLARI_Z		48	/* XGI Volari Z7		*/
#define FB_ACCEL_OMAP1610		49	/* TI OMAP16xx			*/
#define FB_ACCEL_TRIDENT_TGUI		50	/* Trident TGUI			*/
#define FB_ACCEL_TRIDENT_3DIMAGE	51	/* Trident 3DImage		*/
#define FB_ACCEL_TRIDENT_BLADE3D	52	/* Trident Blade3D		*/
#define FB_ACCEL_TRIDENT_BLADEXP	53	/* Trident BladeXP		*/
#define FB_ACCEL_CIRRUS_ALPINE		53	/* Cirrus Logic 543x/544x/5480 (collides with BLADEXP) */
#define FB_ACCEL_NEOMAGIC_NM2070	90	/* NeoMagic NM2070		*/
#define FB_ACCEL_NEOMAGIC_NM2090	91	/* NeoMagic NM2090		*/
#define FB_ACCEL_NEOMAGIC_NM2093	92	/* NeoMagic NM2093		*/
#define FB_ACCEL_NEOMAGIC_NM2097	93	/* NeoMagic NM2097		*/
#define FB_ACCEL_NEOMAGIC_NM2160	94	/* NeoMagic NM2160		*/
#define FB_ACCEL_NEOMAGIC_NM2200	95	/* NeoMagic NM2200		*/
#define FB_ACCEL_NEOMAGIC_NM2230	96	/* NeoMagic NM2230		*/
#define FB_ACCEL_NEOMAGIC_NM2360	97	/* NeoMagic NM2360		*/
#define FB_ACCEL_NEOMAGIC_NM2380	98	/* NeoMagic NM2380		*/
#define FB_ACCEL_PXA3XX			99	/* PXA3xx			*/

#define FB_ACCEL_SAVAGE4		0x80	/* S3 Savage4			*/
#define FB_ACCEL_SAVAGE3D		0x81	/* S3 Savage3D			*/
#define FB_ACCEL_SAVAGE3D_MV		0x82	/* S3 Savage3D-MV		*/
#define FB_ACCEL_SAVAGE2000		0x83	/* S3 Savage2000		*/
#define FB_ACCEL_SAVAGE_MX_MV		0x84	/* S3 Savage/MX-MV		*/
#define FB_ACCEL_SAVAGE_MX		0x85	/* S3 Savage/MX			*/
#define FB_ACCEL_SAVAGE_IX_MV		0x86	/* S3 Savage/IX-MV		*/
#define FB_ACCEL_SAVAGE_IX		0x87	/* S3 Savage/IX			*/
#define FB_ACCEL_PROSAVAGE_PM		0x88	/* S3 ProSavage PM133		*/
#define FB_ACCEL_PROSAVAGE_KM		0x89	/* S3 ProSavage KM133		*/
#define FB_ACCEL_S3TWISTER_P		0x8a	/* S3 Twister			*/
#define FB_ACCEL_S3TWISTER_K		0x8b	/* S3 TwisterK			*/
#define FB_ACCEL_SUPERSAVAGE		0x8c	/* S3 Supersavage		*/
#define FB_ACCEL_PROSAVAGE_DDR		0x8d	/* S3 ProSavage DDR		*/
#define FB_ACCEL_PROSAVAGE_DDRK		0x8e	/* S3 ProSavage DDR-K		*/

#define FB_ACCEL_PUV3_UNIGFX		0xa0	/* PKUnity-v3 Unigfx		*/

#define FB_CAP_FOURCC			1	/* Device supports FOURCC-based formats */

/**
 * struct fb_fix_screeninfo - fixed (non-modifiable) framebuffer information
 * @id:          identification string, e.g. "TT Builtin"
 * @smem_start:  start of frame buffer memory (physical address)
 * @smem_len:    length of frame buffer memory in bytes
 * @type:        one of FB_TYPE_*
 * @type_aux:    aux info; meaning depends on @type (see FB_AUX_*)
 * @visual:      one of FB_VISUAL_*
 * @xpanstep:    zero if no hardware panning
 * @ypanstep:    zero if no hardware panning
 * @ywrapstep:   zero if no hardware ywrap
 * @line_length: length of one line in bytes
 * @mmio_start:  start of memory-mapped I/O region (physical address)
 * @mmio_len:    length of memory-mapped I/O region in bytes
 * @accel:       accel identifier (see FB_ACCEL_*)
 * @capabilities: bitmask of FB_CAP_*
 * @reserved:    must be zeroed by the kernel
 *
 * NOTE: @smem_start and @mmio_start are `unsigned long`, which differs
 * in size between 32-bit and 64-bit. The kernel provides the compat
 * type `struct fb_fix_screeninfo32` (in <linux/fb.h>) for 32-bit
 * userspace; do not change these fields to __u32/__u64.
 */
struct fb_fix_screeninfo {
	char		id[16];		/* identification string eg "TT Builtin" */
	unsigned long	smem_start;	/* Start of frame buffer mem */
					/* (physical address) */
	__u32		smem_len;	/* Length of frame buffer mem */
	__u32		type;		/* see FB_TYPE_*		*/
	__u32		type_aux;	/* Interleave for interleaved Planes */
	__u32		visual;		/* see FB_VISUAL_*		*/
	__u16		xpanstep;	/* zero if no hardware panning	*/
	__u16		ypanstep;	/* zero if no hardware panning	*/
	__u16		ywrapstep;	/* zero if no hardware ywrap	*/
	__u32		line_length;	/* length of a line in bytes	*/
	unsigned long	mmio_start;	/* Start of Memory Mapped I/O	*/
					/* (physical address) */
	__u32		mmio_len;	/* Length of Memory Mapped I/O	*/
	__u32		accel;		/* Indicate to driver which	*/
					/*  specific chip/card we have	*/
	__u16		capabilities;	/* see FB_CAP_*			*/
	__u16		reserved[2];	/* Reserved for future compatibility */
};

/**
 * struct fb_bitfield - position and size of a color component in a pixel
 * @offset:     bit offset from the least significant bit of the pixel value
 * @length:     number of significant bits
 * @msb_right:  if non-zero, the most significant bit is on the right
 *
 * Interpretation of offsets for color fields: all offsets are from the
 * right, inside a "pixel" value which is exactly @bits_per_pixel wide
 * (i.e. you can use the offset as the right operand to `<<`). A pixel
 * afterwards is a bit stream and is written to video memory as that
 * unmodified bitstream.
 *
 * For pseudocolor: @offset and @length should be the same for all color
 * components. @offset specifies the position of the least significant
 * bit of the palette index in a pixel value. @length indicates the
 * number of available palette entries (i.e. # of entries = 1 << length).
 */
struct fb_bitfield {
	__u32	offset;		/* beginning of bitfield		*/
	__u32	length;		/* length of bitfield			*/
	__u32	msb_right;	/* != 0 : Most significant bit is */ 
				/* right */ 
};

/* ======================================================================
 * fb_var_screeninfo.nonstd values
 * ====================================================================== */

#define FB_NONSTD_HAM		1	/* Hold-And-Modify (HAM)	*/
#define FB_NONSTD_REV_PIX_IN_B	2	/* order of pixels in each byte is reversed */

/* ======================================================================
 * fb_var_screeninfo.activate values
 *
 * The low 4 bits are a value field (FB_ACTIVATE_*_masked), and the high
 * bits are independent flags. Use FB_ACTIVATE_MASK to extract the value.
 * ====================================================================== */

#define FB_ACTIVATE_NOW		0	/* set values immediately (or vbl) */
#define FB_ACTIVATE_NXTOPEN	1	/* activate on next open	*/
#define FB_ACTIVATE_TEST	2	/* don't set, round up impossible */
#define FB_ACTIVATE_MASK	15	/* mask for the value part	*/
#define FB_ACTIVATE_VBL		16	/* activate values on next vbl	*/
#define FB_CHANGE_CMAP_VBL	32	/* change colormap on vbl	*/
#define FB_ACTIVATE_ALL		64	/* change all VCs on this fb	*/
#define FB_ACTIVATE_FORCE	128	/* force apply even when no change */
#define FB_ACTIVATE_INV_MODE	256	/* invalidate videomode		*/
#define FB_ACTIVATE_KD_TEXT	512	/* for KDSET vt ioctl		*/

/* (OBSOLETE) see fb_info.flags and vc_mode */
#define FB_ACCELF_TEXT		1

/* ======================================================================
 * fb_var_screeninfo.sync values
 * ====================================================================== */

#define FB_SYNC_HOR_HIGH_ACT	1	/* horizontal sync high active	*/
#define FB_SYNC_VERT_HIGH_ACT	2	/* vertical sync high active	*/
#define FB_SYNC_EXT		4	/* external sync		*/
#define FB_SYNC_COMP_HIGH_ACT	8	/* composite sync high active	*/
#define FB_SYNC_BROADCAST	16	/* broadcast video timings	*/
					/* vtotal = 144d/288n/576i => PAL  */
					/* vtotal = 121d/242n/484i => NTSC */
#define FB_SYNC_ON_GREEN	32	/* sync on green */

/* ======================================================================
 * fb_var_screeninfo.vmode values
 *
 * As with FB_ACTIVATE_*, the low bits are the mode value (use
 * FB_VMODE_MASK to extract) and the high bits are independent flags.
 *
 * NOTE: FB_VMODE_SMOOTH_XPAN and FB_VMODE_CONUPDATE are intentionally
 * the same value (512); they were introduced as aliases. Do not
 * "fix" this — it is ABI.
 * ====================================================================== */

#define FB_VMODE_NONINTERLACED	0	/* non interlaced */
#define FB_VMODE_INTERLACED	1	/* interlaced	*/
#define FB_VMODE_DOUBLE		2	/* double scan */
#define FB_VMODE_ODD_FLD_FIRST	4	/* interlaced: top line first */
#define FB_VMODE_MASK		255

#define FB_VMODE_YWRAP		256	/* ywrap instead of panning	*/
#define FB_VMODE_SMOOTH_XPAN	512	/* smooth xpan possible (internally used) */
#define FB_VMODE_CONUPDATE	512	/* don't update x/yoffset	*/

/*
 * Display rotation support
 */
#define FB_ROTATE_UR	0
#define FB_ROTATE_CW	1
#define FB_ROTATE_UD	2
#define FB_ROTATE_CCW	3

/*
 * Frequency / period conversion helpers.
 *
 *   PICOS2KHZ(p_ps): period in picoseconds -> frequency in kHz
 *                    freq_kHz = 1e9 / p_ps
 *   KHZ2PICOS(f_kHz): frequency in kHz -> period in picoseconds
 *                    p_ps = 1e9 / f_kHz
 *
 * Both formulas are mathematically the same (1e9 is the product of the
 * 1e3 kHz-to-Hz scaling and the 1e6 ps-to-us scaling), so the macros
 * share the same expression.
 */
#define PICOS2KHZ(a)	(1000000000UL / (a))
#define KHZ2PICOS(a)	(1000000000UL / (a))

/**
 * struct fb_var_screeninfo - modifiable framebuffer information
 * @xres:           visible resolution, x
 * @yres:           visible resolution, y
 * @xres_virtual:   virtual resolution, x
 * @yres_virtual:   virtual resolution, y
 * @xoffset:        offset from virtual to visible, x
 * @yoffset:        offset from virtual to visible, y
 * @bits_per_pixel: bits per pixel
 * @grayscale:      0 = color, 1 = grayscale, >1 = FOURCC
 * @red:            red bitfield (see struct fb_bitfield)
 * @green:          green bitfield
 * @blue:           blue bitfield
 * @transp:         transparency bitfield
 * @nonstd:         != 0: non-standard pixel format (see FB_NONSTD_*)
 * @activate:       activation flags (see FB_ACTIVATE_*)
 * @height:         height of picture in mm
 * @width:          width of picture in mm
 * @accel_flags:    (OBSOLETE) see fb_info.flags
 * @pixclock:       pixel clock in ps (pico seconds)
 * @left_margin:    time from sync to picture
 * @right_margin:   time from picture to sync
 * @upper_margin:   time from sync to picture
 * @lower_margin:   time from picture to sync
 * @hsync_len:      length of horizontal sync
 * @vsync_len:      length of vertical sync
 * @sync:           see FB_SYNC_*
 * @vmode:          see FB_VMODE_*
 * @rotate:         counter-clockwise rotation angle (see FB_ROTATE_*)
 * @colorspace:     colorspace for FOURCC-based modes
 * @reserved:       must be zeroed by the kernel
 *
 * Timing fields (@pixclock .. @vsync_len) are all in pixclocks except
 * @pixclock itself.
 */
struct fb_var_screeninfo {
	__u32		xres;			/* visible resolution		*/
	__u32		yres;
	__u32		xres_virtual;		/* virtual resolution		*/
	__u32		yres_virtual;
	__u32		xoffset;		/* offset from virtual to visible */
	__u32		yoffset;		/* resolution			*/

	__u32		bits_per_pixel;		/* guess what			*/
	__u32		grayscale;		/* 0 = color, 1 = grayscale,	*/
						/* >1 = FOURCC			*/
	struct fb_bitfield red;			/* bitfield in fb mem if true color, */
	struct fb_bitfield green;		/* else only length is significant */
	struct fb_bitfield blue;
	struct fb_bitfield transp;		/* transparency			*/

	__u32		nonstd;			/* != 0 Non standard pixel format */

	__u32		activate;		/* see FB_ACTIVATE_*		*/

	__u32		height;			/* height of picture in mm	*/
	__u32		width;			/* width of picture in mm	*/

	__u32		accel_flags;		/* (OBSOLETE) see fb_info.flags */

	/* Timing: All values in pixclocks, except pixclock (of course) */
	__u32		pixclock;		/* pixel clock in ps (pico seconds) */
	__u32		left_margin;		/* time from sync to picture	*/
	__u32		right_margin;		/* time from picture to sync	*/
	__u32		upper_margin;		/* time from sync to picture	*/
	__u32		lower_margin;
	__u32		hsync_len;		/* length of horizontal sync	*/
	__u32		vsync_len;		/* length of vertical sync	*/
	__u32		sync;			/* see FB_SYNC_*		*/
	__u32		vmode;			/* see FB_VMODE_*		*/
	__u32		rotate;			/* angle we rotate counter clockwise */
	__u32		colorspace;		/* colorspace for FOURCC-based modes */
	__u32		reserved[4];		/* Reserved for future compatibility */
};

/**
 * struct fb_cmap - color palette for pseudocolor / directcolor modes
 * @start:  first entry index
 * @len:    number of entries
 * @red:    red values (userspace pointer)
 * @green:  green values (userspace pointer)
 * @blue:   blue values (userspace pointer)
 * @transp: transparency values (userspace pointer, may be NULL)
 *
 * The four arrays must each have at least @len entries. When passed in
 * from userspace the pointers refer to userspace memory; the kernel
 * copies to/from internal storage as needed.
 */
struct fb_cmap {
	__u32		start;		/* First entry		*/
	__u32		len;		/* Number of entries	*/
	__u16		*red;		/* Red values		*/
	__u16		*green;
	__u16		*blue;
	__u16		*transp;	/* transparency, can be NULL */
};

/**
 * struct fb_con2fbmap - console-to-framebuffer mapping
 * @console:     console number
 * @framebuffer: framebuffer device number
 */
struct fb_con2fbmap {
	__u32	console;
	__u32	framebuffer;
};

/* ======================================================================
 * Blanking levels
 *
 * Values are derived from VESA DPMS states (see <linux/vesa.h>). The
 * "+1" offset exists so that FB_BLANK_NORMAL (which is not a VESA
 * state) fits between UNBLANK and VSYNC_SUSPEND.
 * ====================================================================== */

enum {
	/* screen: unblanked, hsync: on,  vsync: on */
	FB_BLANK_UNBLANK       = VESA_NO_BLANKING,

	/* screen: blanked,   hsync: on,  vsync: on */
	FB_BLANK_NORMAL        = VESA_NO_BLANKING + 1,

	/* screen: blanked,   hsync: on,  vsync: off */
	FB_BLANK_VSYNC_SUSPEND = VESA_VSYNC_SUSPEND + 1,

	/* screen: blanked,   hsync: off, vsync: on */
	FB_BLANK_HSYNC_SUSPEND = VESA_HSYNC_SUSPEND + 1,

	/* screen: blanked,   hsync: off, vsync: off */
	FB_BLANK_POWERDOWN     = VESA_POWERDOWN + 1
};

/* ======================================================================
 * fb_vblank.flags values
 * ====================================================================== */

#define FB_VBLANK_VBLANKING	0x001	/* currently in a vertical blank */
#define FB_VBLANK_HBLANKING	0x002	/* currently in a horizontal blank */
#define FB_VBLANK_HAVE_VBLANK	0x004	/* vertical blanks can be detected */
#define FB_VBLANK_HAVE_HBLANK	0x008	/* horizontal blanks can be detected */
#define FB_VBLANK_HAVE_COUNT	0x010	/* global retrace counter is available */
#define FB_VBLANK_HAVE_VCOUNT	0x020	/* the vcount field is valid */
#define FB_VBLANK_HAVE_HCOUNT	0x040	/* the hcount field is valid */
#define FB_VBLANK_VSYNCING	0x080	/* currently in a vsync */
#define FB_VBLANK_HAVE_VSYNC	0x100	/* vertical syncs can be detected */

/**
 * struct fb_vblank - vertical blanking status (FBIOGET_VBLANK)
 * @flags:  bitmask of FB_VBLANK_*; indicates current state and which
 *          of the count fields below are valid
 * @count:  counter of retraces since boot
 * @vcount: current scanline position
 * @hcount: current scandot position
 * @reserved: must be zeroed by the kernel
 */
struct fb_vblank {
	__u32	flags;			/* FB_VBLANK flags */
	__u32	count;			/* counter of retraces since boot */
	__u32	vcount;			/* current scanline position */
	__u32	hcount;			/* current scandot position */
	__u32	reserved[4];		/* reserved for future compatibility */
};

/* ======================================================================
 * Internal HW accel ROPs
 * ====================================================================== */

#define ROP_COPY	0
#define ROP_XOR		1

/**
 * struct fb_copyarea - framebuffer copy area (used by fb_copyarea())
 * @dx:     destination x
 * @dy:     destination y
 * @width:  width in pixels
 * @height: height in pixels
 * @sx:     source x
 * @sy:     source y
 */
struct fb_copyarea {
	__u32	dx;
	__u32	dy;
	__u32	width;
	__u32	height;
	__u32	sx;
	__u32	sy;
};

/**
 * struct fb_fillrect - framebuffer fill rectangle
 * @dx:     screen-relative x
 * @dy:     screen-relative y
 * @width:  width in pixels
 * @height: height in pixels
 * @color:  fill color
 * @rop:    raster operation (ROP_COPY or ROP_XOR)
 */
struct fb_fillrect {
	__u32	dx;	/* screen-relative */
	__u32	dy;
	__u32	width;
	__u32	height;
	__u32	color;
	__u32	rop;
};

/**
 * struct fb_image - image blit descriptor
 * @dx:       x position of top-left corner
 * @dy:       y position of top-left corner
 * @width:    image width
 * @height:   image height
 * @fg_color: foreground color (only used for 1-bit depth)
 * @bg_color: background color (only used for 1-bit depth)
 * @depth:    bits per pixel of @data
 * @data:     userspace pointer to image pixel data
 * @cmap:     color map to use if @depth is not a direct color depth
 *
 * The @data pointer refers to userspace memory; the kernel copies the
 * bitmap before using it. The same applies to the pointers inside @cmap.
 */
struct fb_image {
	__u32		dx;		/* Where to place image */
	__u32		dy;
	__u32		width;		/* Size of image */
	__u32		height;
	__u32		fg_color;	/* Only used when a mono bitmap */
	__u32		bg_color;
	__u8		depth;		/* Depth of the image */
	const char	*data;		/* Pointer to image data */
	struct fb_cmap	cmap;		/* color map info */
};

/* ======================================================================
 * Hardware cursor control (FB_CUR_* flags in struct fb_cursor.set)
 * ====================================================================== */

#define FB_CUR_SETIMAGE	0x01
#define FB_CUR_SETPOS	0x02
#define FB_CUR_SETHOT	0x04
#define FB_CUR_SETCMAP	0x08
#define FB_CUR_SETSHAPE	0x10
#define FB_CUR_SETSIZE	0x20
#define FB_CUR_SETALL	0xFF

/**
 * struct fbcurpos - cursor position, in pixels
 * @x: x coordinate
 * @y: y coordinate
 */
struct fbcurpos {
	__u16 x, y;
};

/**
 * struct fb_cursor - hardware cursor control (FBIO_CURSOR)
 * @set:    bitmask of FB_CUR_* indicating which fields to update
 * @enable: cursor on/off
 * @rop:    bitop operation (ROP_COPY or ROP_XOR)
 * @mask:   userspace pointer to cursor mask bits (1 = opaque)
 * @hot:    cursor hot spot within the image
 * @image:  cursor image
 *
 * The @mask pointer and any pointers inside @image are userspace
 * addresses and are copied by the kernel before use.
 */
struct fb_cursor {
	__u16		set;		/* what to set */
	__u16		enable;		/* cursor on/off */
	__u16		rop;		/* bitop operation */
	const char	*mask;		/* cursor mask bits */
	struct fbcurpos	hot;		/* cursor hot spot */
	struct fb_image	image;		/* Cursor image */
};

/* ======================================================================
 * Settings for the generic backlight code
 * ====================================================================== */

#define FB_BACKLIGHT_LEVELS	128
#define FB_BACKLIGHT_MAX	0xFF

#endif /* _UAPI_LINUX_FB_H */