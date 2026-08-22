#pragma once
#include "core/IDecoder.hpp"
#include <string>
#include <map>
#include <memory>
#include <vector>

namespace sdr {

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    bool load_plugins(const std::string& plugin_dir);
    void unload_all();

    IDecoder* get_decoder(const std::string& name) const;
    std::vector<std::string> decoder_names() const;

private:
    std::map<std::string, std::unique_ptr<IDecoder>> m_decoders;
};

} // namespace sdr
