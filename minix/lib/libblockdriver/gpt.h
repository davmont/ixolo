#ifndef _BLOCKDRIVER_GPT_H
#define _BLOCKDRIVER_GPT_H

#define GPT_PROTECTIVE	0xEE	/* MBR partition type of a protective MBR */

/* A used GPT entry: first and last sector (inclusive), relative to the disk. */
struct gpt_part {
	u64_t first;
	u64_t last;
};

int gpt_protective(const struct part_entry *table);
int gpt_partition(struct blockdriver *bdp, int device, u64_t base,
	u64_t limit, struct device *dv, u8_t *tmp_buf);

#endif /* _BLOCKDRIVER_GPT_H */
