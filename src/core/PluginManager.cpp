#include "core/PluginManager.hpp"
#include <iostream>
#include <dlfcn.h>
#include <filesystem>
#include <dirent.h>

namespace sdr {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    unload_all();
}

bool PluginManager::load_plugins(const std::string& plugin_dir) {
    std::cout << "PluginManager: scanning " << plugin_dir << std::endl;
    if (plugin_dir.empty()) return false;

    // Scan directory for .so / .dylib files
    DIR* dir = opendir(plugin_dir.c_str());
    if (!dir) {
        std::cerr << "PluginManager: cannot open directory " << plugin_dir << std::endl;
        return false;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.find(".so") != std::string::npos || filename.find(".dylib") != std::string::npos) {
            std::string full_path = plugin_dir + "/" + filename;
            void* handle = dlopen(full_path.c_str(), RTLD_NOW);
            if (!handle) {
                std::cerr << "PluginManager: dlopen failed for " << full_path << ": " << dlerror() << std::endl;
                continue;
            }

            using Factory = IDecoder*();
            Factory* factory = reinterpret_cast<Factory*>(dlsym(handle, "create_decoder"));
            if (!factory) {
                std::cerr << "PluginManager: dlsym failed for " << full_path << std::endl;
                dlclose(handle);
                continue;
            }

            IDecoder* decoder = factory();
            if (decoder) {
                m_decoders[decoder->get_name()] = std::unique_ptr<IDecoder>(decoder);
            }
        }
    }
    closedir(dir);
    return true;
}

void PluginManager::unload_all() {
    m_decoders.clear();
}

IDecoder* PluginManager::get_decoder(const std::string& name) const {
    auto it = m_decoders.find(name);
    return (it != m_decoders.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> PluginManager::decoder_names() const {
    std::vector<std::string> names;
    for (const auto& pair : m_decoders) names.push_back(pair.first);
    return names;
}

} // namespace sdr
