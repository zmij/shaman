/**
 * @file config.in.hpp
 *
 *  Configuration header for tests.
 *
 *  Created on: 11.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef OPENGAMES_CONFIG_IN_HPP_
#define OPENGAMES_CONFIG_IN_HPP_

#include <string>

namespace shaman {
namespace test {

const std::string UNCOMPRESSED_FILE  = "@TEST_UNCOMPRESSED_FILE@";
const std::string COMPRESSED_FILE = "@TEST_COMPRESSED_FILE@";


} // namespace test
} // namespace shaman

#endif /* OPENGAMES_CONFIG_IN_HPP_ */
