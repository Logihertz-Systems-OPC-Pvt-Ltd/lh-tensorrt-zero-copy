#ifndef LH_EXPORTERS_HPP
#define LH_EXPORTERS_HPP

#include "zerocopy_types.hpp"
#include "dma_buf_pool.hpp"
#include <iostream>

namespace logihertz {

class Exporters {
public:
    /* Posture Audit Exporters */
    static void export_audit_table(const AuditReport &report, std::ostream &os);
    static void export_audit_json(const AuditReport &report, std::ostream &os);
    static void export_audit_sarif(const AuditReport &report, std::ostream &os);

    /* Layout Geometry Exporters */
    static void export_layout_table(const DmaPlaneLayout &layout, std::ostream &os);
    static void export_layout_json(const DmaPlaneLayout &layout, std::ostream &os);

    /* Pipeline Simulation Exporters */
    static void export_pipeline_sim_table(const DmaBufRingPool &pool, std::ostream &os);
    static void export_pipeline_sim_json(const DmaBufRingPool &pool, std::ostream &os);

    /* Tensor Conversion Exporters */
    static void export_tensor_table(const NCHWTensor &tensor, std::ostream &os);
    static void export_tensor_json(const NCHWTensor &tensor, std::ostream &os);
};

} // namespace logihertz

#endif // LH_EXPORTERS_HPP
