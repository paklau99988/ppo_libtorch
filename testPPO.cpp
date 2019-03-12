#include <fstream>
#include <Eigen/Core>
#include <torch/torch.h>
#include "ProximalPolicyOptimization.h"
#include "Models.h"
#include "TestEnvironment.h"

int main() {

    // Random engine.
    std::random_device rd;
    std::mt19937 re(rd());
    std::uniform_int_distribution<> dist(-5, 5);

    // Environment.
    double x = double(dist(re)); // goal x pos
    double y = double(dist(re)); // goal y pos
    TestEnvironment env(x, y);

    // Model.
    uint n_in = 4;
    uint n_out = 2;
    double std = 1e-2;

    ActorCritic ac(n_in, n_out, std);
    ac.to(torch::kF64);
    ac.normal(0., std);
    torch::optim::Adam opt(ac.parameters(), 1e-3);

    // Training loop.
    uint n_iter = 10000;
    uint n_steps = 64;
    uint n_epochs = 20;
    uint mini_batch_size = 16;
    uint ppo_epochs = uint(n_steps/mini_batch_size);

    VT states;
    VT actions;
    VT rewards;
    VT dones;

    VT log_probs;
    VT returns;
    VT values;

    // Output.
    std::ofstream out;
    out.open("../data/data.csv");

    // episode, agent_x, agent_y, goal_x, goal_y, STATUS=(PLAYING, WON, LOST)
    out << 1 << ", " << env.pos_(0) << ", " << env.pos_(1) << ", " << env.goal_(0) << ", " << env.goal_(1) << ", " << RESETTING << "\n";

    // Counter.
    uint c = 0;

    for (uint e=0;e<n_epochs;e++)
    {
        printf("epoch %u/%u\n", e+1, n_epochs);

        for (uint i=0;i<n_iter;i++)
        {
            // State of env.
            states.push_back(env.State());

            // Play.
            auto av = ac.forward(states[c]);
            actions.push_back(std::get<0>(av));
            values.push_back(std::get<1>(av));
            log_probs.push_back(ac.log_prob(actions[c]));

            double x_act = *(actions[c].data<double>());
            double y_act = *(actions[c].data<double>()+1);
            auto sd = env.Act(x_act, y_act);

            // New state.
            rewards.push_back(env.Reward(std::get<1>(sd)));
            dones.push_back(std::get<2>(sd));

            // episode, agent_x, agent_y, goal_x, goal_y, AGENT=(PLAYING, WON, LOST)
            out << e+1 << ", " << env.pos_(0) << ", " << env.pos_(1) << ", " << env.goal_(0) << ", " << env.goal_(1) << ", " << std::get<1>(sd) << "\n";

            if (*(dones[c].data<double>()) == 1.) 
            {
                // Set new goal.
                double x_new = double(dist(re)); 
                double y_new = double(dist(re));
                env.SetGoal(x_new, y_new);

                // Reset the position of the agent.
                env.Reset();

                // episode, agent_x, agent_y, goal_x, goal_y, STATUS=(PLAYING, WON, LOST)
                out << e+1 << ", " << env.pos_(0) << ", " << env.pos_(1) << ", " << env.goal_(0) << ", " << env.goal_(1) << ", " << RESETTING << "\n";
            }

            c++;

            // Update.
            if (c%n_steps == 0)
            {
                values.push_back(std::get<1>(ac.forward(states[c-1])));

                returns = PPO::returns(rewards, dones, values, .99, .95);

                torch::Tensor t_log_probs = torch::cat(log_probs).detach();
                torch::Tensor t_returns = torch::cat(returns).detach();
                torch::Tensor t_values = torch::cat(values).detach();
                torch::Tensor t_states = torch::cat(states);
                torch::Tensor t_actions = torch::cat(actions);
                torch::Tensor t_advantages = t_returns - t_values.slice(0, 0, n_steps);

                PPO::update(ac, t_states, t_actions, t_log_probs, t_returns, t_advantages, opt, n_steps, ppo_epochs, mini_batch_size);
            
                c = 0;

                states.clear();
                actions.clear();
                rewards.clear();
                dones.clear();

                log_probs.clear();
                returns.clear();
                values.clear();
            }
        }

        // Reset at the end of an epoch.
        double x_new = double(dist(re)); 
        double y_new = double(dist(re));
        env.SetGoal(x_new, y_new);

        // Reset the position of the agent.
        env.Reset();

        // episode, agent_x, agent_y, goal_x, goal_y, STATUS=(PLAYING, WON, LOST)
        out << e+1 << ", " << env.pos_(0) << ", " << env.pos_(1) << ", " << env.goal_(0) << ", " << env.goal_(1) << ", " << RESETTING << "\n";
    }

    out.close();

    return 0;
}
