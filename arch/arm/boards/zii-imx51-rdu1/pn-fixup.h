#ifndef __ZII_PN_FIXUP__
#define __ZII_PN_FIXUP__

struct zii_pn_fixup {
	const char *pn;
	void (*callback) (void);
};

int zii_process_dds_fixups(const struct zii_pn_fixup *, size_t);

#endif	/* __ZII_PN_FIXUP__ */