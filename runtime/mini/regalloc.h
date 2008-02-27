
typedef size_t regmask_t;

enum {
	MONO_REG_FREE,
	MONO_REG_FREEABLE,
	MONO_REG_MOVEABLE,
	MONO_REG_BUSY,
	MONO_REG_RESERVED
};

enum {
	MONO_REG_INT,
	MONO_REG_DOUBLE
};

typedef struct {
	/* symbolic registers */
	int next_vreg;

	/* hard registers */
	int num_iregs;
	int num_fregs;

	regmask_t ifree_mask;
	regmask_t ffree_mask;

	/* symbolic -> hard register assignment */
	/* 
	 * If the register is spilled, then this contains -spill - 1, where 'spill'
	 * is the index of the spill variable.
	 */
	int *vassign;

	/* hard -> symbolic */
	int isymbolic [MONO_MAX_IREGS];
	int fsymbolic [MONO_MAX_FREGS];

	int ispills;

	int vassign_size;
} MonoRegState;

#define mono_regstate_next_int(rs)   ((rs)->next_vreg++)
#define mono_regstate_next_float(rs) ((rs)->next_vreg++)


MonoRegState* mono_regstate_new (void) MONO_INTERNAL;

void          mono_regstate_free      (MonoRegState *rs) MONO_INTERNAL;
void          mono_regstate_reset     (MonoRegState *rs) MONO_INTERNAL;
inline int    mono_regstate_next_long (MonoRegState *rs) MONO_INTERNAL;
