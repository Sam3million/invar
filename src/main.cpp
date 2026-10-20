#include "App.h"
#include "argparse.hpp"
#include "Config.h"

int main(int argc, char* argv[])
{
    argparse::ArgumentParser args("Invar");
    args.add_argument("--scene").default_value(std::string("models/Sponza/glTF/Sponza.gltf"));
    args.add_argument("--res").default_value(std::string("2560x1440"));
    args.add_argument("--bench").default_value(false).implicit_value(true);
    args.add_argument("--frames").scan<'i', int>().default_value(1000);
    args.add_argument("--warmup").scan<'i', int>().default_value(120);
    args.add_argument("--repeats").scan<'i', int>().default_value(5);
    args.add_argument("--mode").default_value(std::string("meshlet"));
    args.add_argument("--inFlight").scan<'i', int>().choices("1", "2", "3").default_value(3);
    try
    {
        args.parse_args(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n" << args;
        return 1;
    }

    Config cfg;
    cfg.scene = args.get<std::string>("--scene");
    cfg.mode = args.get<std::string>("--mode");
    std::sscanf(args.get<std::string>("--res").c_str(), "%ux%u", &cfg.width, &cfg.height);
    cfg.bench = args.get<bool>("--bench");
    cfg.frames = args.get<int>("--frames");
    cfg.repeats = args.get<int>("--repeats");
    cfg.framesInFlight = args.get<int>("--inFlight");
    cfg.warmup = args.get<int>("--warmup");

    App app(cfg);
    app.run();

    return 0;
}
