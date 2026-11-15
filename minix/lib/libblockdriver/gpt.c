/*
 * GUID Partition Table (GPT) support for partition(): when a disk's MBR is
 * only a protective one, the primary partitions p0..p3 come from the GPT.
 *
 * What is supported, and what is not:
 *  - Only a purely protective MBR (one entry of type 0xEE, the others empty)
 *    makes us read the GPT.  A hybrid MBR, which also lists real partitions,
 *    keeps being read as an MBR, as before: that is how releasetools/mkgpt.py
 *    images expose their root file system, and the MBR entries there are what
 *    the boot loader and the partition tools agree on.
 *  - The first four used GPT entries, in table order, become p0..p3.  Further
 *    entries are not reachable (a message says so).  GPT partitions are not
 *    subpartitioned.
 *  - 512-byte logical sectors only, which is all this library handles.
 *  - The header and the entry array are checked: signature, sizes, the CRC32
 *    of both.  If the primary header is bad, the backup one at the end of the
 *    disk is tried.  Without a valid GPT, the disk is left as the MBR says.
 */

#include <minix/blockdriver.h>
#include <minix/drvlib.h>
#include <string.h>

#include "gpt.h"

#define GPT_SIGNATURE	"EFI PART"
#define GPT_HDR_MIN	92		/* bytes of the header we use */
#define GPT_ENT_MIN	128		/* minimum size of an entry */
#define GPT_MAX_ENTRIES	1024		/* refuse anything larger */

/* Offsets in the header. */
#define H_SIG		0
#define H_HDRSIZE	12
#define H_HDRCRC	16
#define H_MYLBA		24
#define H_FIRSTUSE	40
#define H_LASTUSE	48
#define H_ENTLBA	72
#define H_NENT		80
#define H_ENTSIZE	84
#define H_ENTCRC	88

/* Offsets in an entry. */
#define E_TYPE		0		/* type GUID, all zero if unused */
#define E_FIRST		32		/* first LBA */
#define E_LAST		40		/* last LBA, inclusive */

static u32_t
get32(const u8_t *p)
{
	return (u32_t)p[0] | ((u32_t)p[1] << 8) | ((u32_t)p[2] << 16) |
	    ((u32_t)p[3] << 24);
}

static u64_t
get64(const u8_t *p)
{
	return (u64_t)get32(p) | ((u64_t)get32(p + 4) << 32);
}

/*
 * CRC32 as used by GPT (IEEE 802.3, reflected).  Call with crc = 0 to start,
 * then pass the previous result to continue over more data.
 */
static u32_t
crc32_update(u32_t crc, const u8_t *p, size_t len)
{
	int k;

	crc = ~crc;
	while (len-- > 0) {
		crc ^= *p++;
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
	}
	return ~crc;
}

/*
 * Read CD_SECTOR_SIZE bytes at the given sector into tmp_buf.  Return the
 * number of bytes read (less near the end of the device), or 0 on error.
 */
static size_t
read_at(struct blockdriver *bdp, int device, u64_t sector, u8_t *tmp_buf)
{
	iovec_t iov;
	ssize_t r;

	iov.iov_addr = (vir_bytes)tmp_buf;
	iov.iov_size = CD_SECTOR_SIZE;
	r = (*bdp->bdr_transfer)(device, FALSE /*do_write*/,
	    sector * SECTOR_SIZE, SELF, &iov, 1, BDEV_NOFLAGS);
	return (r > 0) ? (size_t)r : 0;
}

/*
 * Is this MBR partition table only a protective one?
 */
int
gpt_protective(const struct part_entry *table)
{
	int i, prot = 0;

	for (i = 0; i < NR_PARTITIONS; i++) {
		if (table[i].sysind == GPT_PROTECTIVE)
			prot++;
		else if (table[i].sysind != NO_PART)
			return 0;
	}
	return prot == 1;
}

/*
 * Read and check the GPT header at the given sector of a disk of 'nsec'
 * sectors.  On success, fill in the fields we need and return 1.
 */
static int
read_header(struct blockdriver *bdp, int device, u64_t lba, u64_t nsec,
	u8_t *tmp_buf, u64_t *entlba, u32_t *nent, u32_t *entsize,
	u32_t *entcrc, u64_t *firstuse, u64_t *lastuse)
{
	u32_t hdrsize, crc;

	if (read_at(bdp, device, lba, tmp_buf) < SECTOR_SIZE)
		return 0;
	if (memcmp(tmp_buf + H_SIG, GPT_SIGNATURE, 8) != 0)
		return 0;
	hdrsize = get32(tmp_buf + H_HDRSIZE);
	if (hdrsize < GPT_HDR_MIN || hdrsize > SECTOR_SIZE)
		return 0;
	crc = get32(tmp_buf + H_HDRCRC);
	memset(tmp_buf + H_HDRCRC, 0, 4);
	if (crc32_update(0, tmp_buf, hdrsize) != crc)
		return 0;
	if (get64(tmp_buf + H_MYLBA) != lba)
		return 0;

	*entlba = get64(tmp_buf + H_ENTLBA);
	*nent = get32(tmp_buf + H_NENT);
	*entsize = get32(tmp_buf + H_ENTSIZE);
	*entcrc = get32(tmp_buf + H_ENTCRC);
	*firstuse = get64(tmp_buf + H_FIRSTUSE);
	*lastuse = get64(tmp_buf + H_LASTUSE);

	if (*nent == 0 || *nent > GPT_MAX_ENTRIES)
		return 0;
	if (*entsize < GPT_ENT_MIN || *entsize > CD_SECTOR_SIZE ||
	    (*entsize % 8) != 0 || (CD_SECTOR_SIZE % *entsize) != 0)
		return 0;
	if (*entlba < 2 || *entlba >= nsec ||
	    *entlba + ((u64_t)*nent * *entsize + SECTOR_SIZE - 1) /
	    SECTOR_SIZE > nsec)
		return 0;
	if (*firstuse > *lastuse || *lastuse >= nsec)
		return 0;
	return 1;
}

/*
 * Read the entry array and pick the first NR_PARTITIONS used entries.  The
 * CRC is checked over the whole array before anything is used.  Return the
 * number of used entries in the whole table, or -1 if the array is bad.
 */
static int
read_entries(struct blockdriver *bdp, int device, u64_t entlba, u32_t nent,
	u32_t entsize, u32_t entcrc, u64_t firstuse, u64_t lastuse,
	u8_t *tmp_buf, struct gpt_part *parts)
{
	static const u8_t zero_guid[16];
	u64_t sector, first, last;
	size_t left, chunk, got, off;
	u32_t crc, i;
	int used, pass;

	/* Two passes: the first checks the CRC, the second takes the
	 * entries, so that nothing from a corrupt table is used.
	 */
	for (pass = 0; pass < 2; pass++) {
		crc = 0;
		used = 0;
		i = 0;
		sector = entlba;
		left = (size_t)nent * entsize;
		while (left > 0) {
			chunk = (left < CD_SECTOR_SIZE) ? left : CD_SECTOR_SIZE;
			got = read_at(bdp, device, sector, tmp_buf);
			if (got < chunk)
				return -1;
			if (pass == 0)
				crc = crc32_update(crc, tmp_buf, chunk);
			else for (off = 0; off < chunk; off += entsize, i++) {
				const u8_t *e = tmp_buf + off;

				if (memcmp(e + E_TYPE, zero_guid, 16) == 0)
					continue;
				first = get64(e + E_FIRST);
				last = get64(e + E_LAST);
				if (first < firstuse || last > lastuse ||
				    first > last)
					continue;	/* nonsense: skip */
				if (used < NR_PARTITIONS) {
					parts[used].first = first;
					parts[used].last = last;
				}
				used++;
			}
			left -= chunk;
			sector += CD_SECTOR_SIZE / SECTOR_SIZE;
		}
		if (pass == 0 && crc != entcrc)
			return -1;
	}
	return used;
}

/*
 * Read the GPT of a disk whose MBR is protective, and set its primary
 * partitions from it.  'base' and 'limit' are the disk's first and last-plus-
 * one sectors, as in parse_part_table().  'dv' is the array of the four
 * primary partition devices.  Return 1 if a valid GPT was found and used, 0
 * if not (the partitions are then left alone).
 */
int
gpt_partition(struct blockdriver *bdp, int device, u64_t base, u64_t limit,
	struct device *dv, u8_t *tmp_buf)
{
	struct gpt_part parts[NR_PARTITIONS];
	u64_t nsec, entlba, firstuse, lastuse, first, end;
	u32_t nent, entsize, entcrc;
	int i, used;

	nsec = limit - base;
	if (nsec < 4)
		return 0;

	/* The primary header is in sector 1; the backup in the last one. */
	if (!read_header(bdp, device, 1, nsec, tmp_buf, &entlba, &nent,
	    &entsize, &entcrc, &firstuse, &lastuse) ||
	    (used = read_entries(bdp, device, entlba, nent, entsize, entcrc,
	    firstuse, lastuse, tmp_buf, parts)) < 0) {
		if (!read_header(bdp, device, nsec - 1, nsec, tmp_buf,
		    &entlba, &nent, &entsize, &entcrc, &firstuse, &lastuse) ||
		    (used = read_entries(bdp, device, entlba, nent, entsize,
		    entcrc, firstuse, lastuse, tmp_buf, parts)) < 0)
			return 0;
		printf("blockdriver: d%d: the primary GPT is damaged; using "
		    "the backup\n", device / DEV_PER_DRIVE);
	}

	if (used > NR_PARTITIONS)
		printf("blockdriver: d%d: GPT has %d partitions; only the "
		    "first %d are available\n", device / DEV_PER_DRIVE,
		    used, NR_PARTITIONS);

	for (i = 0; i < NR_PARTITIONS; i++, dv++) {
		if (i >= used) {
			dv->dv_base = 0;
			dv->dv_size = 0;
			continue;
		}
		/* Relative to the disk; keep within it, as for the MBR. */
		first = base + parts[i].first;
		end = base + parts[i].last + 1;
		if (end > limit)
			end = limit;
		if (first > end)
			first = end;
		dv->dv_base = first * SECTOR_SIZE;
		dv->dv_size = (end - first) * SECTOR_SIZE;
	}
	return 1;
}
