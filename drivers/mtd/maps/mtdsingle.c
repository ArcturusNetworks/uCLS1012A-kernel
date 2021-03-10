/****************************************************************************
 *
 *   mtdsingle.c - creates single mtd partition on the fly by `insmod`
 *                 `rmmod` will remove this partition
 *                 May be useful to update a non-standart image
 *
 *   Copyright (c) 2021 Arcturus Networks Inc.
 *                 by Oleksandr Zhadan <www.ArcturusNetworks.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

static char *pname = "TEMP";
module_param(pname, charp, 0);
MODULE_PARM_DESC(pname, "Partition Name");

static int pbase = 0;		/* default - start of the flash    */
module_param(pbase, int, 0);
MODULE_PARM_DESC(pbase, "Partition Base Address");

static int psize = 0;		/* default - all flash             */
module_param(psize, int, 0);
MODULE_PARM_DESC(psize, "Partition Size");

static struct mtd_info *dev_mtd = NULL;
static int mtdnr = 0;

struct mtd_part {
	struct mtd_info mtd;
	struct mtd_info *parent;
	uint64_t offset;
	struct list_head list;
};

static int __init mtdsingle_init(void)
{
	struct mtd_info *mtd;

	/* lets find out the last used mtd index */
	for (mtdnr = 0;; mtdnr++) {
		mtd = get_mtd_device(NULL, mtdnr);
		if (IS_ERR(mtd)) {
			if (mtdnr == 0) {
				mtdnr = -1;
				return -EIO;
			}
			break;
		}
	}
	/* Use /dev/mtd0 to get parent */
	mtd = get_mtd_device(NULL, 0);
	pr_debug("Child : mtd%d: %8.8llx \"%s\"\n", mtd->index,
		 (unsigned long long)mtd->size, mtd->name);

	if (mtd_is_partition(mtd))
		dev_mtd = (container_of(mtd, struct mtd_part, mtd))->parent;
	else
		dev_mtd = mtd;
	pr_debug("Parent: mtd%d: %8.8llx \"%s\"\n", dev_mtd->index,
		 (unsigned long long)dev_mtd->size, dev_mtd->name);

	/* add extra partition to the parent */
	mtd_add_partition(dev_mtd, pname, pbase, psize);

	return 0;
}

static void __exit mtdsingle_exit(void)
{
	mtd_del_partition(dev_mtd, mtdnr);
}

module_init(mtdsingle_init);
module_exit(mtdsingle_exit);

MODULE_DESCRIPTION
    ("Partition creation module for the non-standart update image");
MODULE_AUTHOR("Oleksandr Zhadan <www.ArcturusNetworks.com>");
MODULE_LICENSE("GPL");
