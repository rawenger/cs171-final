//
// Created by ryan on 5/13/23.
//

#include <iostream>
#include <sstream>
#include <cereal/archives/json.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/archives/binary.hpp>

struct test {
        struct test2 {
                int b;
                int c;
                int d;
                template <class Archive>
                void serialize(Archive &ar)
                { ar(b, c, d); }
        };
        int a;
        double d;
        char c;
        test2 t2;

        template <class Archive>
        void serialize(Archive &ar)
        { ar(a, d, c, t2); }
};

struct test3 {
        int a;
        int b;
        void serialize(cereal::PortableBinaryOutputArchive &ar)
        { ar(a, b); }
};

int main()
{
        test t {1, 2.0, '3', {4, 5, 6}};
        test3 t3 {1,2};
        std::stringstream json, xml, pbin, bin, bin2;
        {
                cereal::JSONOutputArchive jout{json};
                cereal::XMLOutputArchive xout{xml};
                cereal::PortableBinaryOutputArchive pbout{pbin};
                cereal::BinaryOutputArchive bout{bin};
                cereal::BinaryOutputArchive bout2{bin2};

                jout(CEREAL_NVP(t));
                xout(CEREAL_NVP(t));
                pbout(CEREAL_NVP(t));
                bout(CEREAL_NVP(t));
                bout2(CEREAL_NVP(t));
        }

        std::cout << "JSON:\n"
                << json.str() << std::endl;

        std::cout << "===================" << std::endl;

        std::cout << "XML:\n"
                << xml.str() << std::endl;

        std::cout << "===================" << std::endl;

        std::cout << "PORTBIN:\n"
                  << pbin.str() << std::endl;

        std::cout << "===================" << std::endl;

        std::cout << "BIN:\n"
                  << bin.str() << std::endl;

        std::cout << "===================" << std::endl;

        std::cout << "BIN non-template:\n"
                  << bin2.str() << std::endl;

        return 0;
}