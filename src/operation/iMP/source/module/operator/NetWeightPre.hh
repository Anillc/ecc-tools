#include <vector>
#include <string>

namespace imp {
	struct SubPath {
    std::string outputPin;
    std::string inputPin;
    double delay;
  }; 
  struct TimingPath {
    std::string beginpoint;
    std::string endpoint;
    double slack_time;
    std::vector<SubPath> sub_paths;
    int path_depth;
  };

	std::vector<TimingPath> parseTimingReport(const std::string& filePath);
  std::vector<std::string> splitByPath(const std::string& str);
  TimingPath parsePathBlock(const std::string& block, bool& skip_path);
  std::vector<std::string> splitString(const std::string& str, char delimiter);
  std::string trim(const std::string& str);
  std::string processBrackets(const std::string& input);
	
}  // namespace imp