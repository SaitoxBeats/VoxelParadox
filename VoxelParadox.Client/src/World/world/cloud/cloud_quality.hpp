#pragma once

#include <string>

namespace VoxelGame::Clouds {

enum class CloudQuality {
  LOW,
  MEDIUM,
  HIGH,
};

const char* cloudQualityId(CloudQuality quality);
const char* cloudQualityDisplayName(CloudQuality quality);
bool tryParseCloudQuality(const std::string& value, CloudQuality& outQuality);

} // namespace VoxelGame::Clouds
