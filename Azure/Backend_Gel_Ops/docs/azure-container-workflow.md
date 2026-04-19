# VS Code Workflow for Azure Container Deployments

This guide mirrors the previous routine you used with the original App Service Python image while applying the updated Dockerfile. It keeps the flow inside Visual Studio Code and finishes inside the Azure portal, where the existing Gunicorn startup command remains in the Deployment Center.

## Prerequisites

- Docker Desktop running locally.
- Visual Studio Code with the **Docker** extension installed and signed in to your Azure subscription.
- Access to the target Azure Container Registry (ACR) and App Service.
- Updated `Dockerfile` in this repository.

## 1. Build the image locally from VS Code

1. In VS Code Explorer, right-click the `Dockerfile` and choose **Build Image** (context menu provided by the Docker extension).
2. Accept the default image name or provide one in the format `myacr.azurecr.io/backend-gel-ops:local`.
3. Monitor the integrated terminal to ensure the build succeeds with no errors.

> Tip: If you need to rebuild after changes, right-click the `Dockerfile` again and repeat **Build Image**.

## 2. Push the image to Azure Container Registry using the Docker extension

1. In the **Docker** view (left activity bar), expand **Images → Local**.
2. Locate the freshly built image tag, right-click it, and select **Push**.
3. When prompted, pick your Azure Container Registry (ACR). Sign in if VS Code prompts for Azure credentials.
4. Wait for the push to complete; you should see the new tag listed under **Registries → Azure → <your ACR> → Repositories** in the Docker view.

> If the ACR connection is not already present, you can add it by clicking the plug icon in the Docker view and selecting **Connect Registry → Azure**.

## 3. Assign the new image in Azure App Service Deployment Center

1. Open the [Azure portal](https://portal.azure.com/) and browse to your App Service.
2. Navigate to **Deployment Center → Container**.
3. Choose **Azure Container Registry** as the source, then select:
   - Your registry
   - The `backend-gel-ops` repository (or the name you used when pushing)
   - The new image tag
4. Save the configuration. App Service will restart and pull the updated container.
5. Verify under **Settings → General settings** that your existing Gunicorn startup command (for example, `gunicorn gellert_project.wsgi:application --bind=0.0.0.0:8000 --workers=4 --log-file=logs/gunicorn.log`) is still set. Update only if you intentionally need to change it.

## 4. Post-deployment checks

- Confirm the site loads as expected.
- Review **Logs → Log stream** for successful Gunicorn startup.
- Update configuration values (SAML metadata, secrets, etc.) in **Settings → Configuration** if the image changes require it.

This routine keeps everything as close as possible to your previous workflow while using the modernized container image.
