# Git & Dev Container Infrastructure

This document outlines the standard operating procedure for initializing a local repository, configuring secure SSH authentication via Host Agent Forwarding, and linking to the remote GitHub repository.

## Phase 1: Local Repository Initialization (Dev Container)

Run these commands inside your VS Code Dev Container terminal to establish the local source control.

```bash
# Initialize git 
git init

# Rename master to main 
git branch -M main

# Configure identity for this local container environment 
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

*Architect's Note:* Ensure your files (like `.gitignore`, `CMakeLists.txt`, `sdkconfig.defaults`) are staged before committing.

```bash
# Create the initial commit 
git commit -m "Initial project structure and infrastructure setup"
```

---

## Phase 2: Host SSH Key Generation & Persistence (Host Machine)

To avoid losing SSH keys when the Docker container rebuilds, keys must reside on the **Host OS** (Ubuntu) and be forwarded to the container. Run these steps on your physical Host terminal.

1. **Generate the Key:**
```bash
ssh-keygen -t ed25519 -C "your.email@example.com"
```



(This matches the generation command you used, but executing it on the Host ensures permanence).


2. **Automate the SSH Agent:**
Add this to your Host's `~/.bashrc` to ensure the agent runs automatically:
```bash
# Robust SSH agent startup.
# Checks ssh-add -l instead of $SSH_AUTH_SOCK to handle two failure modes:
#   1. No agent running ($SSH_AUTH_SOCK is unset).
#   2. Stale socket: $SSH_AUTH_SOCK is set but points to a dead agent from
#      a previous session — a silent failure that breaks git push.
if ! ssh-add -l &>/dev/null; then
    eval "$(ssh-agent -s)" > /dev/null
    ssh-add ~/.ssh/id_ed25519
fi
```


Apply it with `source ~/.bashrc`.

**Architect's Note (Preferred Alternative):** Rather than the `.bashrc` hook, add the following to `~/.ssh/config`. This delegates agent management to `ssh` itself on first use and is more robust across shell environments (zsh, non-interactive shells, WSL2):
```
Host github.com
    AddKeysToAgent yes
    IdentityFile ~/.ssh/id_ed25519
```
3. **Retrieve the Public Key:**
```bash
cat ~/.ssh/id_ed25519.pub
```


Copy the output and **Add to GitHub**  under your account's SSH settings.



---

## Phase 3: Container Integration & Remote Linking (Dev Container)

Rebuild or reopen your VS Code Dev Container. The container will now automatically inherit the `SSH_AUTH_SOCK` from your host.

1. **Verify Authentication:**
Inside the Dev Container terminal, test the connection:


```bash
ssh -T git@github.com
```


*Expected Output: `Hi YOUR_GITHUB_USER! You've successfully authenticated...*`
2. **Link the Local Repo to the Remote URL:**


Note: While your initial notes used the HTTPS URL, we apply the SSH URL here to enforce our Tier-1 security standard.


```bash
# Link the local repo to the remote URL 
git remote add origin git@github.com:YOUR_GITHUB_USER/selective_pet_access.git

# Verify the link (optional but good habit) 
git remote -v
```


3. **Push to the Cloud:**
```bash
# Push your 'main' branch to the server and set upstream tracking 
git push -u origin main
```

---

