#ifndef LH_TRT_AUDITOR_HPP
#define LH_TRT_AUDITOR_HPP

#include "zerocopy_types.hpp"

namespace logihertz {

class TrtAuditor {
public:
    static AuditReport evaluate_posture(const PolicyConfig &policy);
};

} // namespace logihertz

#endif // LH_TRT_AUDITOR_HPP
