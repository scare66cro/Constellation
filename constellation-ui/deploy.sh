ssh gellert@10.1.2.122 "rm -r ~/Gellert/ui-svelte/client/*; rm -r ~/Gellert/ui-svelte/server/*"
scp -r build/* gellert@10.1.2.122:Gellert/ui-svelte
ssh gellert@10.1.2.122 "sudo service uisvelte restart"