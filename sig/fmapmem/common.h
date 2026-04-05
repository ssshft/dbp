#ifndef FMAPMEM_COMMON_H
#define FMAPMEM_COMMON_H

#include <cstdlib>
#include <string>
#include <unordered_map>

#define DECLARE_PTR(X) typedef std::shared_ptr<X> X##_ptr;   /** define smart ptr */
#define FORWARD_DECLARE_PTR(X) class X; DECLARE_PTR(X)      /** forward defile smart ptr */

namespace fmapmem
{
    namespace journal
    {
        class journal_error : public std::runtime_error
        {
        public:
            journal_error(const std::string &message) : runtime_error(message)
            {}
        };
    }
}


#endif //FMAPMEM_COMMON_H
