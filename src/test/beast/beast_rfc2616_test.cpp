//------------------------------------------------------------------------------
/*
This file is part of rippled: https://github.com/ripple/rippled
Copyright (c) 2012, 2013 Ripple Labs Inc.

Permission to use, copy, modify, and/or distribute this software for any
purpose  with  or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

//==============================================================================

#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/xor_shift_engine.h>

namespace beast {

class rfc2616_test : public unit_test::suite
{
public:
    std::string
    concatenate(
        std::vector<std::string> const& words,
        std::string const& pre = {},
        std::string const& mid = {},
        std::string const& post = {})
    {
        std::string text;
        for (auto const& word : words)
        {
            text += pre;

            if (mid.empty())
                text += word;
            else
            {
                std::string s {word};
                std::size_t pos {0};
                while ((pos = s.find(' ', pos)) != std::string::npos)
                {
                    s.insert(pos, mid);
                    pos += mid.size() + 1;
                }
                text += s;
            }

            text += post + ',';
        }
        text.pop_back();

        return text;
    }

    void run() override
    {
        testcase("LWS compression & trimming during parsing");

        using namespace beast::rfc2616;
        std::vector<std::string> const words {
            "apple", "star fruit", "juicy juniper berry" };

        // no added space
        {
            auto const text {concatenate(words)};
            BEAST_EXPECT(split_commas(text) == words);
        }

        for (auto const& space : {" ", "     "})
        {
            // prefix
            {
                auto const text {concatenate(words, space)};
                BEAST_EXPECT(split_commas(text) == words);
            }

            // mid
            {
                auto const text {concatenate(words, "", space)};
                BEAST_EXPECT(split_commas(text) == words);
            }

            // suffix
            {
                auto const text {concatenate(words, "", "", space)};
                BEAST_EXPECT(split_commas(text) == words);
            }

            // prefix, mid
            {
                auto const text {concatenate(words, space, space)};
                BEAST_EXPECT(split_commas(text) == words);
            }

            // prefix, mid, suffix
            {
                auto const text {concatenate(words, space, space, space)};
                BEAST_EXPECT(split_commas(text) == words);
            }

            // prefix, suffix
            {
                auto const text{ concatenate(words, space, "", space) };
                BEAST_EXPECT(split_commas(text) == words);
            }

            // mid, suffix
            {
                auto const text {concatenate(words, "", space, space)};
                BEAST_EXPECT(split_commas(text) == words);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(rfc2616, utility, beast);

}
