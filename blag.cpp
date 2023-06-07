#include <chrono>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "fmt/core.h"

#include "blag.h"

auto blag::transact(transaction trans) -> void
{
        if (std::holds_alternative<post_transaction>(trans)) {
                // Commit a post to the blog.
                auto now = std::chrono::system_clock::now();
                timestamp stamp = std::chrono::duration_cast<timestamp>(now.time_since_epoch());
                const post_transaction &post_trans = std::get<post_transaction>(trans);
                post post = {
                        .author = post_trans.author,
                        .title = post_trans.title,
                        .body = post_trans.body,
                        .comments = std::vector<comment>(),
                        .stamp = stamp,
                };
                posts.insert(std::make_pair(stamp, post));
        }
        else if (std::holds_alternative<comment_transaction>(trans)) {
                // Commit a comment to the blog.
                const comment_transaction &comm_trans = std::get<comment_transaction>(trans);
                post *comment_on = find_post_with_title(comm_trans.title);
                if (comment_on == nullptr) {
                        // TODO: Silently fail if there is no blog with this title.
                        return;
                }
                comment comment = std::make_pair(comm_trans.commenter, comm_trans.comment);
                comment_on->comments.push_back(comment);
        }
}

auto blag::find_post_with_title(const content &title) -> post *
{
        for (auto &&finger = posts.begin(); finger != posts.end(); ++finger) {
                if (finger->second.title == title) {
                        return &finger->second;
                }
        }

        return nullptr;
}

// Make a new blog post identified by the given title. 
auto blag::new_post(user author, content title, content body) -> void
{
        // TODO: Don't default-initialize, find the magical candidate function that does it in place.
        transaction trans;
        post_transaction post_trans = {
                .author = author,
                .title = title,
                .body = body,
        };
        trans = post_trans;
        transact(trans);
}

// Make a new comment under the blog post with the given title.
auto blag::new_comment(content title, user commenter, content comment) -> void
{
        transaction trans;
        comment_transaction comm_trans = {
                .commenter = commenter,
                .title = title,
                .comment = comment,
        };
        trans = comm_trans;
        transact(trans);
}

// List the title and author of all blog posts in chronological order.
auto blag::all_posts(std::ostream &out) -> void
{
        if (posts.size() > 0) {
                for (auto &&finger = posts.begin(); finger != posts.end(); ++finger) {
                        const post &post = finger->second;
                        out << fmt::format("@{}: '{}' -- ", post.author, post.title);
                        out << "[stamped: " << post.stamp << "]" << std::endl; // TODO: Good god the format library.
                }
        } else {
                out << "<no posts in blog>" << std::endl;
        }
}

// List the title and content of all blog posts made by this user in chronological order.
auto blag::all_posts_by(user author, std::ostream &out) -> void
{
        size_t found = 0;
        for (auto &&finger = posts.begin(); finger != posts.end(); ++finger) {
                const post &post = finger->second;
                if (post.author == author) {
                        out << fmt::format("@{}: '{}' -- ", post.author, post.title);
                        out << "[stamped: " << post.stamp << "]" << std::endl; // TODO: Good god the format library.
                        out << "    > " << post.body << std::endl;
                        found += 1;
                }
        }

        if (found < 1) {
                out << fmt::format("<no posts by @{}>", author) << std::endl;
        }
}

// List the content of the given blog post and its comments, including commenter and content.
auto blag::view_comments(content title, std::ostream &out) -> void
{
        const post *post = find_post_with_title(title);

        if (post == nullptr) {
                out << fmt::format("<no post titled '{}'>", title) << std::endl;
        } else {
                out << fmt::format("@{}: '{}' -- ", post->author, post->title);
                out << "[stamped: " << post->stamp << "]" << std::endl; // TODO: Good god the format library.
                out << "    > " << post->body << std::endl;
                if (post->comments.size() > 0) {
                        for (const comment &comment : post->comments) {
                                out << "    " << fmt::format("@{}: {}", comment.first, comment.second) << std::endl;
                        }
                } else {
                        out << "    " << "@cricket: just me in here? lol" << std::endl;
                }
        }

}

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
