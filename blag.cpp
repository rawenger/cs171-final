#include <chrono>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/chrono.h>

#include "blag.h"

blag blag::BLAG;

auto blag::transact(const transaction &trans) -> bool
{
        if (std::holds_alternative<post_transaction>(trans))
        {
                // Commit a post to the blog.
                const auto &pt = std::get<post_transaction>(trans);
                post post = {
                        .author = pt.author,
                        .title = pt.title,
                        .body = pt.body,
                        .comments = {},
                        .stamp = pt.time,
                };

                if (find_post_with_title(pt.title)) {
                        fmt::print(stderr, "Post titled \"{}\" already exists\n.", pt.title);
                        return false;
                }

                fmt::print("@{} made a new post: \"{}\"\n",
                           pt.author, pt.title);

                posts.insert(std::make_pair(pt.time, post));
        }
        else if (std::holds_alternative<comment_transaction>(trans))
        {
                // Commit a comment to the blog.
                const auto &ct = std::get<comment_transaction>(trans);
                post *comment_on = find_post_with_title(ct.title);
                if (comment_on == nullptr) {
                        fmt::print(stderr, "No such post exists: \"{}\"\n.", ct.title);
                        return false;
                }

                fmt::print("@{} commented on \"{}\"\n",
                           ct.author, ct.title);

                comment comm {ct.author, ct.comment};

                comment_on->comments.push_back(comm);
        }

        return true;
}

auto blag::find_post_with_title(std::string_view title) -> post *
{
        for (auto &[_, post] : posts) {
                if (post.title == title) {
                        return &post;
                }
        }

        return nullptr;
}

// List the title and author of all blog posts in chronological order.
auto blag::all_posts(std::ostream &out) -> void
{
        if (posts.empty()) {
                fmt::print(out, "<no posts in blog>\n");
                return;
        }

        for (auto &&finger = posts.begin(); finger != posts.end(); ++finger) {
                const post &post = finger->second;
                fmt::print(out, "@{}: '{}' -- [stamped: {:%x at %X}]\n",
                           post.author, post.title, post.stamp);
        }
}

// List the title and content of all blog posts made by this user in chronological order.
auto blag::all_posts_by(std::string_view author, std::ostream &out) -> void
{
        bool found = false;
        for (auto finger = posts.begin(); finger != posts.end(); ++finger) {
                const post &post = finger->second;
                if (post.author == author) {
                        fmt::print(out, "@{}: '{}' -- [stamped: {:%x at %X}]\n"
                                        "    > {}\n",
                                   post.author, post.title, post.stamp, post.body);
                        found = true;
                }
        }

        if (!found) {
                fmt::print(out, "<no posts by @{}>\n", author);
        }
}

// List the content of the given blog post and its comments, including commenter and content.
auto blag::view_comments(std::string_view title, std::ostream &out) -> void
{
        const post *post = find_post_with_title(title);

        if (post == nullptr) {
                fmt::print(out, "<no post titled '{}'>\n", title);
        } else {
                fmt::print(out, "@{}: '{}' -- [stamped: {:%x at %X}]\n"
                                "    > {}\n",
                                post->author, post->title, post->stamp, post->body);

                if (post->comments.empty())
                        fmt::print(out, "    @cricket: just me in here? lol\n");

                for (const comment &comment : post->comments)
                        fmt::print(out, "    @{}: {}\n", comment.user, comment.content);
        }
}

std::string format_as(const blag::post_transaction &pt)
{
        constexpr size_t trim_length = 8;
        return fmt::format("(post by @{}: \"{}\")",
                           pt.author,
                           std::string_view{pt.title, trim_length});
}

std::string format_as(const blag::comment_transaction &ct)
{
        constexpr size_t trim_length = 8;
        return fmt::format(R"((comment by @{} on post "{}": "{}"))",
                           ct.author,
                           std::string_view{ct.title, trim_length},
                           std::string_view{ct.comment, trim_length});
}

// see https://en.cppreference.com/w/cpp/utility/variant/visit
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::string format_as(const blag::transaction &tr)
{
        return std::visit(overloaded{
                        [](const auto &arg) { return format_as(arg); }
                }, tr);
}