/**********************************************************
* 文件名: quiz.js
* 日期: 2026-07-17
* 版本: v1.1
* 更新记录: 支持由课程配置正确与错误反馈文案
* 描述: 为课程中的选择题提供即时反馈
**********************************************************/

document.addEventListener("click", (event) => {
  const button = event.target.closest("[data-answer]");
  if (!button) return;

  const quiz = button.closest(".quiz");
  const feedback = quiz.querySelector(".feedback");
  const correct = button.dataset.answer === quiz.dataset.correct;
  feedback.dataset.state = correct ? "right" : "wrong";
  feedback.textContent = correct
    ? quiz.dataset.correctFeedback || "回答正确。"
    : quiz.dataset.wrongFeedback || "请重新检查本课给出的边界和证据。";
});
