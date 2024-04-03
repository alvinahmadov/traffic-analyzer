#ifndef TADS_TILED_DISPLAY_HPP
#define TADS_TILED_DISPLAY_HPP

#include "common.hpp"

enum class TiledDisplayState
{
	DISABLED = 0,
	ENABLED = 1,
	/** When user sets tiler group enable=2,
	 * all sinks with the key: link-only-to-demux=1
	 * shall be linked to demuxer's src_[source_id] pad
	 * where source_id is the key set in this
	 * corresponding [sink] group
	 */
	ENABLED_WITH_PARALLEL_DEMUX = 2
};

struct TiledDisplayBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *tiler;
};

struct TiledDisplayConfig
{
	TiledDisplayState enable;
	uint rows;
	uint columns;
	uint width;
	uint height;
	uint gpu_id;
	NvBufMemoryType nvbuf_memory_type;
	/**Compute Scaling NVENC to use
	 * Applicable only for Jetson; x86 uses GPU by default
	 * (0): Default          - Default, GPU for Tesla, VIC for Jetson
	 * (1): GPU              - GPU
	 * (2): VIC              - VIC
	 *  */
	uint compute_hw;
	uint buffer_pool_size;
};

/**
 * Initialize @ref NvDsTiledDisplayBin. It creates and adds tiling and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_TILED_DISPLAY
 *
 * @param[in] config pointer of type @ref NvDsTiledDisplayConfig
 *            parsed from configuration file.
 * @param[in] bin pointer to @ref NvDsTiledDisplayBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_tiled_display_bin(TiledDisplayConfig *config, TiledDisplayBin *bin);

#endif // TADS_TILED_DISPLAY_HPP
