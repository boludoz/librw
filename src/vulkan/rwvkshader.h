#ifdef RW_VULKAN

#include <string>
#include <vector>
#include <unordered_set>

namespace maple 
{
	class Shader;
}

namespace rw
{
	namespace vulkan
	{
		std::shared_ptr<maple::Shader> getShader(int32_t shader);

		struct Shader
		{
			int32_t shaderId;

			static Shader* create(const std::string & vert,
				const char* userDefine,
				const std::string& frag,
				const char* fragUserDefine, const std::string& dynamics = "");

			static Shader* createComp(const std::string& comp, const char* userDefine = "");


			void use(void);
			void destroy(void);

			void setConstValue(const std::string &name, const void *value);
		};

		extern Shader* currentShader;
	}        // namespace vulkan
}        // namespace rw
#endif
