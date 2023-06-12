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

auto blag::transact(const transaction &trans) -> void
{
        trans.add_to_blag(*this);
}

void blag::post_transaction::add_to_blag(blag &b) const
{
        timestamp stamp = std::chrono::system_clock::now();
        post post = {
                .author = author,
                .title = title,
                .body = content,
                .comments = {},
                .stamp = stamp,
        };

        b.posts.insert(std::make_pair(stamp, post));
}

std::string blag::post_transaction::formatter() const
{
        return fmt::format("(post by @{}: \"{}\")",
                           author,
                           std::string_view{title.c_str(), format_trim_length});
}

void blag::comment_transaction::add_to_blag(blag &b) const
{
        post *comment_on = b.find_post_with_title(title);
        if (comment_on == nullptr) {
                // TODO: Silently fail if there is no blog with this title.
                return;
        }

        comment comm {author, content};

        comment_on->comments.push_back(comm);
}

std::string blag::comment_transaction::formatter() const
{
        return fmt::format(R"((comment by @{} on post "{}": "{}"))",
                           author,
                           std::string_view{title.c_str(), format_trim_length},
                           std::string_view{content.c_str(), format_trim_length});
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

}

std::string format_as(const blag::comment_transaction &ct)
{

}


//template<class... Ts>
//struct overloaded : Ts... { using Ts::operator()...; };
//template<class... Ts>
//overloaded(Ts...) -> overloaded<Ts...>;
//
//std::string format_as(const blag::transaction &tr)
//{
//        return std::visit(overloaded{
//                        [](const auto &arg) { return format_as(arg); }
//                }, tr);
//}

// Make a new blog post identified by the given title.
//auto blag::new_post(user author, content title, content body) -> void
//{
//        // TODO: Don't default-initialize, find the magical candidate function that does it in place.
//        transaction trans;
//        post_transaction post_trans = {
//                .author = author,
//                .title = title,
//                .body = body,
//        };
//        trans = post_trans;
//        transact(trans);
//}

/*
auto dummy_transactions(std::ostream &out) -> void
{
        blag cool_peeps;
        cool_peeps.all_posts(std::cout);
        cool_peeps.new_post(
                "jvns",
                "Some blogging myths",
                "A few years ago I gave a short talk (slides) about myths..."
        );
        cool_peeps.new_post(
                "jvns",
                "Introducing 'Implement DNS in a Weekend'",
                "Hello! I’m excited to announce a project I’ve been working on for a long time..."
        );
        cool_peeps.all_posts_by("jvns", std::cout);
        cool_peeps.all_posts_by("notjvns", std::cout);
        cool_peeps.view_comments("Some blogging myths", std::cout);
        cool_peeps.new_comment("Introducing 'Implement DNS in a Weekend'", "ryan", "i just stole a nameserver from comcast");
        cool_peeps.new_comment("Introducing 'Implement DNS in a Weekend'", "jackson", "cool post julia");
        cool_peeps.view_comments("Introducing 'Implement DNS in a Weekend'", std::cout);
        cool_peeps.new_post(
                "nystrom",
                "Type Checking If Expressions",
                "I have this hobby project I’ve been hacking on for several years. It’s a fantasy console..."
        );
        cool_peeps.all_posts(std::cout);
}
*/

// Make a new comment under the blog post with the given title.
//auto blag::new_comment(content title, user commenter, content comment) -> void
//{
//        transaction trans;
//        comment_transaction comm_trans = {
//                .commenter = std::move(commenter),
//                .title = std::move(title),
//                .comment = std::move(comment),
//        };
//        trans = comm_trans;
//        transact(trans);
//}

