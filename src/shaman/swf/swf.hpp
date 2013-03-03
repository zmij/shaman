#ifndef _SHAMAN_SWF_SWF_HPP_
#define _SHAMAN_SWF_SWF_HPP_

#include <iosfwd>

namespace shaman {
namespace swf {

/**
 * Class representing an SWF file structure.
 * @see SWF file format specification ver. 19 http://wwwimages.adobe.com/www.adobe.com/content/dam/Adobe/en/devnet/swf/pdf/swf-file-format-spec.pdf
 */
class swf {
};

std::ostream&
operator << (std::ostream&, swf const&);

std::istream&
operator >> (std::istream&, swf&);

} // namespace swf
} // namespace shaman

#endif /* _SHAMAN_SWF_SWF_HPP_ */
